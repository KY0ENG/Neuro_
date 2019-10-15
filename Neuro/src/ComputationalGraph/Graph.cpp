﻿#include "ComputationalGraph/Graph.h"
#include "ComputationalGraph/TensorLike.h"
#include "ComputationalGraph/Placeholder.h"
#include "ComputationalGraph/Variable.h"
#include "ComputationalGraph/Operation.h"
#include "Debug.h"
#include "Tools.h"

#define ENABLE_GRAPH_LOGS

#ifdef ENABLE_GRAPH_LOGS
#include <windows.h>
#include <debugapi.h>
#include "Tools.h"
#define GRAPH_DEBUG_INFO(...) do { OutputDebugString(StringFormat(__VA_ARGS__).c_str()); } while(0)
#else
#define GRAPH_DEBUG_INFO(...)
#endif

namespace Neuro
{
    Graph* Graph::s_Default = nullptr;

    //////////////////////////////////////////////////////////////////////////
    Graph::Graph()
    {        
    }

    //////////////////////////////////////////////////////////////////////////
    Neuro::Graph* Graph::Default()
    {
        if (!s_Default)
            s_Default = new Graph();
        return s_Default;
    }

    //////////////////////////////////////////////////////////////////////////
    void Graph::AddVariable(Variable* v)
    {
        m_Variables.push_back(v);
        m_Nodes.push_back(v);
    }

    //////////////////////////////////////////////////////////////////////////
    void Graph::AddPlaceholder(Placeholder* p)
    {
        m_Placeholders.push_back(p);
        m_Nodes.push_back(p);
    }

    //////////////////////////////////////////////////////////////////////////
    void Graph::AddOperation(Operation* op)
    {
        m_Operations.push_back(op);
        m_Nodes.push_back(op);
    }

    //////////////////////////////////////////////////////////////////////////
    void Graph::InitVariables()
    {
        for (auto var : m_Variables)
            var->Initialize();
    }

    //////////////////////////////////////////////////////////////////////////
    void Graph::IncrementStep()
    {
        ++m_CurrentStep;
    }

    //////////////////////////////////////////////////////////////////////////
    vector<TensorLike*> Graph::BuildForwardOrder(const vector<TensorLike*>& endNodes)
    {
        vector<TensorLike*> result;
        unordered_set<TensorLike*> visited;

        for (auto node : endNodes)
            ProcessForwardNode(node, result, visited);

        return result;
    }

    //////////////////////////////////////////////////////////////////////////
    void Graph::ProcessForwardNode(TensorLike* node, vector<TensorLike*>& nodes, unordered_set<TensorLike*>& visited)
    {
        for (auto inputNode : node->m_InputNodes)
            ProcessForwardNode(inputNode, nodes, visited);

        if (!node->IsOp())
            return;

        if (visited.find(node) != visited.end())
            return;

        visited.insert(node);
        nodes.push_back(node);
    }

    //////////////////////////////////////////////////////////////////////////
    vector<TensorLike*> Graph::BuildBackwardOrder(const vector<TensorLike*>& endNodes, const vector<Variable*>& params)
    {
        // we need to figure out which nodes were required to calculate end nodes
        // later on when check if all consumers were visited we will additionally check if
        // any particular consumer is required, otherwise it's inputs' gradients are not important 
        // (and won't be computed anyway)
        auto nodesAffectingLosses = BuildForwardOrder(endNodes);

        // build hash set for fast lookup
        unordered_set<TensorLike*> required;
        required.insert(nodesAffectingLosses.begin(), nodesAffectingLosses.end());

        vector<TensorLike*> result;
        unordered_set<TensorLike*> visited;
        unordered_set<TensorLike*> visitedParams;

        for (auto node : endNodes)
            ProcessBackwardNode(node, result, params, false, visited, visitedParams, required);

        return result;
    }

    //////////////////////////////////////////////////////////////////////////
    void Graph::ProcessBackwardNode(TensorLike* node, vector<TensorLike*>& nodes, const vector<Variable*>& params, bool ignoreConsumersCheck, unordered_set<TensorLike*>& visited, unordered_set<TensorLike*>& visitedParams, const unordered_set<TensorLike*>& required)
    {
        bool allConsumersVisited = true;

        if (!ignoreConsumersCheck)
        {
            // only include node when all consumers are already visited
            // we have to make sure that all consumers' input gradients are computed in order to properly average node's output gradient
            for (auto consumer : node->m_Consumers)
            {
                assert(consumer->IsOp());

                // we don't care about consumers that didn't contribute to losses
                if (required.find(consumer) == required.end())
                    continue;

                if (visited.find(consumer) == visited.end())
                {
                    allConsumersVisited = false;
                    break;
                }
            }
        }

        if (!allConsumersVisited)
            return;

        visited.insert(node);
        nodes.push_back(node);

        // we can stop back propagation as soon as we have visited all desired parameters
        if (!params.empty() && node->IsVar() && find(params.begin(), params.end(), node) != params.end())
        {
            visitedParams.insert(node);
            if (visitedParams.size() == params.size())
                return;
        }

        for (auto inputNode : node->m_InputNodes)
        {
            ProcessBackwardNode(inputNode, nodes, params, false, visited, visitedParams, required);

            if (!params.empty() && visitedParams.size() == params.size())
                return;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    vector<Variable*> Graph::ComputeGradients(const vector<TensorLike*>& losses, const vector<Variable*>& params)
    {
        auto order = BuildBackwardOrder(losses, params);
        return ComputeGradientsInOrder(order, params);
    }

    //////////////////////////////////////////////////////////////////////////
    vector<Variable*> Graph::ComputeGradientsInOrder(const vector<TensorLike*>& order, const vector<Variable*>& params)
    {
        vector<Variable*> variables;
        
        for (size_t n = 0; n < order.size(); ++n)
        {
            if (n + 1 < order.size())
            {
                auto node = order[n + 1];
                GRAPH_DEBUG_INFO("##Graph: Prefetching '%s'...\n", node->Name().c_str());

                // in order to compute any node's gradient we need it's output, inputs (input gradients will simply be allocated by operation)
                node->Output().Prefetch();
                for (auto inputNode : node->m_InputNodes)
                    inputNode->Output().Prefetch();
            }

            auto node = order[n];
            GRAPH_DEBUG_INFO("##Graph: Computing gradient '%s'...\n", node->Name().c_str());

            if (node->IsVar())
            {
                Variable* var = static_cast<Variable*>(node);
                if (var->Trainable() && (params.empty() || find(params.begin(), params.end(), node) != params.end()))
                    variables.push_back(var);
            }

            bool computeGrad = true;

            // there is no reason for computing gradients for contants nor placeholders when they have no input nodes
            // these are not trainable parameters
            if ((node->IsConst() || node->IsPlaceholder()) && node->m_InputNodes.empty())
                computeGrad = false;

            if (computeGrad)
            {
                auto& nodeOutputGrad = node->m_OutputGrad;
                nodeOutputGrad.Resize(node->m_Output.GetShape());
                nodeOutputGrad.Zero(); // reset gradient

                if (node->IsVar() && !static_cast<Variable*>(node)->Trainable())
                    continue;

                int inputGradsUsed = 0;

                for (auto consumer : node->m_Consumers)
                {
                    assert(consumer->IsOp());
                    Operation* consumerOp = static_cast<Operation*>(consumer);

                    auto& inputsGrad = consumerOp->InputsGrads();

                    // ignore consumer when it didn't participate in forward step or its input gradients were not computed
                    // the latter can happen when it wasn't required for loss computation (alternatively we could pass
                    // required nodes to this function and check against that but it would be more involving from 'user'
                    // point of view
                    if (consumerOp->LastComputeStep() != m_CurrentStep || inputsGrad.empty())
                        continue;

                    ++inputGradsUsed;

                    if (inputsGrad.size() == 1)
                    {
                        assert(inputsGrad[0].Length());
                        nodeOutputGrad.Add(inputsGrad[0], nodeOutputGrad);
                    }
                    else
                    {
                        auto nodeIndexInConsumerInputs = distance(consumer->m_InputNodes.begin(), find(consumer->m_InputNodes.begin(), consumer->m_InputNodes.end(), node));
                        auto& lossGradWrtNode = inputsGrad[nodeIndexInConsumerInputs];
                        assert(lossGradWrtNode.Length());
                        nodeOutputGrad.Add(lossGradWrtNode, nodeOutputGrad);
                    }
                }

                if (inputGradsUsed == 0) // it must be one the loss nodes (gradient of loss w.r.t to loss is 1)
                    nodeOutputGrad.One();

                if (node->IsOp())
                    static_cast<Operation*>(node)->ComputeGradient(nodeOutputGrad);
            }

            // all consumers contributing to this node's output grad can be notified so they can release their corresponding input gradient
            for (auto consumerNode : node->m_Consumers)
                consumerNode->InputGradConsumed(node);
        }

        return variables;
    }

    //////////////////////////////////////////////////////////////////////////
    TensorLike* Graph::GetNode(const string& name)
    {
        for (auto node : m_Nodes)
        {
            if (node->Name() == name)
                return node;
        }
        return nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    void Graph::DebugLog()
    {
#ifdef DEBUG_LOG_ENABLED
        for (auto node : m_Operations)
        {
            if (node->LastComputeStep() != m_CurrentStep)
                continue;

            if (Debug::ShouldLogOutput(node->Name()))
                node->Output().DebugDumpValues(Replace(node->Name() + "_step" + to_string(m_CurrentStep) + ".log", "/", "_"));

            if (Debug::ShouldLogGrad(node->Name()))
            {
                auto& inputGrads = static_cast<Operation*>(node)->InputsGrads();
                for (size_t i = 0; i < inputGrads.size(); ++i)
                    inputGrads[i].DebugDumpValues(Replace(node->Name() + "_grad" + to_string(i) + "_step" + to_string(m_CurrentStep) + ".log", "/", "_"));
            }
        }

        for (auto node : m_Variables)
        {
            if (Debug::ShouldLogGrad(node->Name()))
                node->OutputGrad().DebugDumpValues(Replace(node->Name() + "_grad_step" + to_string(m_CurrentStep) + ".log", "/", "_"));
        }
#endif
    }
}

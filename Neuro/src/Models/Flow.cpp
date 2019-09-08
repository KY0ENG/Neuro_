﻿#include <algorithm>
#include <iomanip>
#include <sstream>

#include "Models/Flow.h"
#include "Layers/LayerBase.h"

namespace Neuro
{
	//////////////////////////////////////////////////////////////////////////
	Flow::Flow(const vector<LayerBase*>& inputLayers, const vector<LayerBase*>& outputLayers)
        : ModelBase(__FUNCTION__)
	{
		m_InputLayers = inputLayers;
		m_OutputLayers = outputLayers;
        for (size_t i = 0; i < outputLayers.size(); ++i)
            m_OutputShapes.push_back(outputLayers[i]->OutputShape());
        m_Outputs.resize(outputLayers.size());

        vector<LayerBase*> visited;
        for (auto inputLayer : m_InputLayers)
            ProcessLayer(inputLayer, visited);

        m_ReversedOrder.resize(m_Order.size());
        reverse_copy(m_Order.begin(), m_Order.end(), m_ReversedOrder.begin());
	}

	//////////////////////////////////////////////////////////////////////////
    Flow::~Flow()
    {
        for (auto layer : m_Order)
            delete layer;
    }

    //////////////////////////////////////////////////////////////////////////
    LayerBase* Flow::GetCloneInstance() const
    {
        return new Flow();
    }

    //////////////////////////////////////////////////////////////////////////
	void Flow::ProcessLayer(LayerBase* layer, vector<LayerBase*>& visited)
	{
		bool allInputLayersVisited = true;

		for (auto inLayer : layer->m_InputLayers)
		{
			if (find(visited.begin(), visited.end(), inLayer) == visited.end())
			{
				allInputLayersVisited = false;
				break;
			}
		}

		if (!allInputLayersVisited)
			return;

		m_Order.push_back(layer);
		visited.push_back(layer);

		for (auto outLayer : layer->m_OutputLayers)
			ProcessLayer(outLayer, visited);
	}

	//////////////////////////////////////////////////////////////////////////
	void Flow::FeedForwardInternal(bool training)
	{
		for (size_t i = 0; i < m_InputLayers.size(); ++i)
			m_InputLayers[i]->FeedForward(m_Inputs[i], training);

		for (auto layer : m_Order)
		{
			// layers with no input layers have are already been fed forward
			if (layer->m_InputLayers.size() == 0)
				continue;

			tensor_ptr_vec_t ins(layer->m_InputLayers.size());
			for (size_t i = 0; i < layer->m_InputLayers.size(); ++i)
				ins[i] = &(layer->m_InputLayers[i]->m_Outputs[0]);

			layer->FeedForward(ins, training);
		}

        for (size_t i = 0; i < m_OutputLayers.size(); ++i)
            m_Outputs[i] = m_OutputLayers[i]->Output();
	}

	//////////////////////////////////////////////////////////////////////////
	void Flow::BackPropInternal(vector<Tensor>& outputGradients)
	{
        for (uint32_t i = 0; i < (int)m_OutputLayers.size(); ++i)
        {
            vector<Tensor> grads = { outputGradients[i] };
            m_OutputLayers[i]->BackProp(grads);
        }

		for (auto layer : m_ReversedOrder)
		{
			// layers with no input layers have are already been fed forward
			if (layer->m_OutputLayers.size() == 0)
				continue;

			Tensor avgInputGradient(layer->m_OutputShapes[0]);
			for (size_t i = 0; i < layer->m_OutputLayers.size(); ++i)
			{
				// we need to find this layer index in output layer's inputs to grab proper delta (it could be cached)
				for (size_t j = 0; j < layer->m_OutputLayers[i]->m_InputLayers.size(); ++j)
				{
					if (layer->m_OutputLayers[i]->m_InputLayers[j] == layer)
					{
						avgInputGradient.Add(layer->m_OutputLayers[i]->m_InputGradients[j], avgInputGradient);
						break;
					}
				}
			}

			avgInputGradient.Div((float)layer->m_OutputLayers.size(), avgInputGradient);
            vector<Tensor> avgGrads = { avgInputGradient };
            layer->BackProp(avgGrads);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	const vector<LayerBase*>& Flow::GetOutputLayers() const
	{
		return m_OutputLayers;
	}

	//////////////////////////////////////////////////////////////////////////
	uint32_t Flow::GetOutputLayersCount() const
	{
		return (uint32_t)m_OutputLayers.size();
	}

	//////////////////////////////////////////////////////////////////////////
	void Flow::OnClone(const LayerBase& source)
	{
        __super::OnClone(source);

        auto& sourceFlow = static_cast<const Flow&>(source);

		// clone is not a frequently used functionality so I'm not too concerned about its performance

		// make clones first and store then in dictionary
		map<string, LayerBase*> clones;

		for (auto layer : sourceFlow.m_Order)
		{
			auto clone = layer->Clone();
			clones[clone->Name()] = clone;
		}

		// then connect them in the same manner as in original network and clone order
		for (auto layer : sourceFlow.m_Order)
		{
			auto layerClone = clones[layer->Name()];
			for (auto inLayer : layer->InputLayers())
			{
				auto inLayerClone = clones[inLayer->Name()];
				layerClone->m_InputLayers.push_back(inLayerClone);
				inLayerClone->m_OutputLayers.push_back(layerClone);
			}

			m_Order.push_back(layerClone);
		}

		m_ReversedOrder.resize(m_Order.size());
		reverse_copy(m_Order.begin(), m_Order.end(), m_ReversedOrder.begin());

        for (auto layer : sourceFlow.m_InputLayers)
		{
			auto layerClone = clones[layer->Name()];
			m_InputLayers.push_back(layerClone);
		}

		for (auto layer : sourceFlow.m_OutputLayers)
		{
			auto layerClone = clones[layer->Name()];
			m_OutputLayers.push_back(layerClone);
		}
	}

	//////////////////////////////////////////////////////////////////////////
	const vector<LayerBase*>& Flow::GetLayers() const
	{
		return m_Order;
	}
}

#include "ComputationalGraph/Operations/BatchNormalizeOp.h"

namespace Neuro
{
    //////////////////////////////////////////////////////////////////////////
    BatchNormalizeOp::BatchNormalizeOp(TensorLike* x, TensorLike* gamma, TensorLike* beta, TensorLike* runningMean, TensorLike* runningVar, float momentum, float epsilon, TensorLike* training, const string& name)
        : Operation({ x, gamma, beta, runningMean, runningVar, training }, name.empty() ? "batch_normalize" : name), m_Epsilon(epsilon), m_Momentum(momentum)
    {
        m_Output.Resize(x->GetShape());
    }

    //////////////////////////////////////////////////////////////////////////
    void BatchNormalizeOp::ComputeInternal()
    {
        auto& x = *m_Inputs[0];
        auto& gamma = *m_Inputs[1];
        auto& beta = *m_Inputs[2];
        auto& runningMean = m_InputNodes[3]->Output();
        auto& runningVar = m_InputNodes[4]->Output();
        bool training = (*m_Inputs[5])(0) != 0;

        m_Output.ResizeBatch(m_Inputs[0]->Batch());

        if (training)
            m_Inputs[0]->BatchNormalizationTrain(gamma, beta, m_Momentum, m_Epsilon, runningMean, runningVar, m_SaveMean, m_SaveInvVar, m_Output);
        else
            m_Inputs[0]->BatchNormalization(gamma, beta, m_Epsilon, runningMean, runningVar, m_Output);
    }

    //////////////////////////////////////////////////////////////////////////
    void BatchNormalizeOp::ComputeGradientInternal(const Tensor& grad)
    {
        auto& x = *m_Inputs[0];
        auto& gamma = *m_Inputs[1];
        auto& beta = *m_Inputs[2];

        grad.BatchNormalizationGradient(x, gamma, m_Epsilon, grad, m_SaveMean, m_SaveInvVar, m_InputsGrads[1], m_InputsGrads[2], true, m_InputsGrads[0]);
    }
}
#include <algorithm>
#include "ComputationalGraph/Operations/SubtractOp.h"

namespace Neuro
{
    //////////////////////////////////////////////////////////////////////////
    SubtractOp::SubtractOp(TensorLike* a, TensorLike* b, const string& name)
        : Operation({ a, b }, name.empty() ? "sub" : name)
    {
        m_Output.Resize(Shape(max(a->GetShape().Width(), b->GetShape().Width()), max(a->GetShape().Height(), b->GetShape().Height()), max(a->GetShape().Depth(), b->GetShape().Depth())));
    }

    //////////////////////////////////////////////////////////////////////////
    void SubtractOp::ComputeInternal()
    {
        m_Output.ResizeBatch(max(m_Inputs[0]->Batch(), m_Inputs[1]->Batch()));
        return m_Inputs[0]->Sub(*m_Inputs[1], m_Output);
    }

    //////////////////////////////////////////////////////////////////////////
    void SubtractOp::ComputeGradientInternal(const Tensor& grad)
    {
        auto& a = *m_Inputs[0];
        auto& b = *m_Inputs[1];

        auto gradWrtA = grad;
        auto gradWrtB = grad.Negated();

        for (int i = WidthAxis; i <= BatchAxis; ++i)
        {
            if (gradWrtA.Len(i) != 1 && a.Len(i) == 1)
                gradWrtA = sum(gradWrtA, (EAxis)i);

            if (gradWrtB.Len(i) != 1 && b.Len(i) == 1)
                gradWrtB = sum(gradWrtB, (EAxis)i);
        }

        gradWrtA.CopyTo(m_InputsGrads[0]);
        gradWrtB.CopyTo(m_InputsGrads[1]);
    }
}
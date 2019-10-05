#include "ComputationalGraph/Operations/ConcatenateOp.h"

namespace Neuro
{
    //////////////////////////////////////////////////////////////////////////
    ConcatenateOp::ConcatenateOp(const vector<TensorLike*>& xs, EAxis axis, const string& name)
        : Operation(xs, name.empty() ? "concatenate" : name), m_Axis(axis)
    {
        auto sumDims = [&](const vector<TensorLike*>& xs, size_t dim)
        {
            uint32_t sum = 0;
            for (auto x : xs)
                sum += x->GetShape().Len(dim);
            return sum;
        };

        if (m_Axis == WidthAxis)
            m_Output.Resize(Shape(sumDims(xs, 0), xs[0]->GetShape().Height(), xs[0]->GetShape().Depth()));
        else if (m_Axis == HeightAxis)
            m_Output.Resize(Shape(xs[0]->GetShape().Width(), sumDims(xs, 1), xs[0]->GetShape().Depth()));
        else if (m_Axis == DepthAxis)
            m_Output.Resize(Shape(xs[0]->GetShape().Width(), xs[0]->GetShape().Height(), sumDims(xs, 2)));
        else if (m_Axis == BatchAxis)
            m_Output.Resize(Shape(xs[0]->GetShape().Width(), xs[0]->GetShape().Height(), xs[0]->GetShape().Depth()));
        else
            assert(false);
    }

    //////////////////////////////////////////////////////////////////////////
    void ConcatenateOp::ComputeInternal()
    {
        if (m_Axis == BatchAxis)
            m_Output.ResizeBatch((uint32_t)m_Inputs.size());
        Tensor::Concat(m_Axis, m_Inputs, m_Output);
    }

    //////////////////////////////////////////////////////////////////////////
    void ConcatenateOp::ComputeGradientInternal(const Tensor& grad)
    {
        grad.Split(m_Axis, m_InputsGradsPtrs);
    }
}
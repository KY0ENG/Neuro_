#pragma once

#include "ComputationalGraph/Operation.h"

namespace Neuro
{
    class RollOp : public Operation
    {
    public:
        RollOp(TensorLike* x, int rollX, int rollY, const string& name = "");

    protected:
        virtual void ComputeInternal() override;
        virtual void ComputeGradientInternal(const Tensor& grad) override;

    private:
        int m_RollX;
        int m_RollY;
    };

    static Operation* roll(TensorLike* x, int rollX, int rollY, const string& name = "")
    {
        return new RollOp(x, rollX, rollY, name);
    }

    class RandomRollOp : public Operation
    {
    public:
        RandomRollOp(TensorLike* x, uint32_t jitterScale = 1, const string& name = "");

    protected:
        virtual void ComputeInternal() override;
        virtual void ComputeGradientInternal(const Tensor& grad) override;

    private:
        uint32_t m_JitterScale;
        int m_LastRollX;
        int m_LastRollY;
    };

    static Operation* random_roll(TensorLike* x, uint32_t jitterSize = 1, const string& name = "")
    {
        return new RandomRollOp(x, jitterSize, name);
    }
}

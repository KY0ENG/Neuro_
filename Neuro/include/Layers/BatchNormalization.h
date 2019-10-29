#pragma once

#include "Layers/SingleLayer.h"

namespace Neuro
{
    class Variable;

    class BatchNormalization : public SingleLayer
    {
    public:
        // Make sure to link this layer to input when using this constructor.
        BatchNormalization(const string& name = "");
        // This constructor should only be used for input layer
        BatchNormalization(const Shape& inputShape, const string& name = "");

        virtual void CopyParametersTo(LayerBase& target, float tau) const override;
        virtual void Parameters(vector<Variable*>& params, bool onlyTrainable = true) const override;

        BatchNormalization* SetMomentum(float momentum);

    protected:
        BatchNormalization(const string& constructorName, const Shape& inputShape, const string& name = "");
        BatchNormalization(bool) {}

        virtual LayerBase* GetCloneInstance() const override;
        
        virtual void Build(const vector<Shape>& inputShapes) override;
        virtual vector<TensorLike*> InternalCall(const vector<TensorLike*>& inputs, TensorLike* training) override;

    protected:
        Variable* m_Gamma;
        Variable* m_Beta;

        Variable* m_RunningMean;
        Variable* m_RunningVar;

        float m_Momentum = 0.99f;
        float m_Epsilon = 0.001f;
    };
}

﻿#pragma once

#include "Optimizers/OptimizerBase.h"

namespace Neuro
{
    // Implementation based on https://github.com/tensorflow/tensorflow/blob/master/tensorflow/python/training/adam.py
    class Adam : public OptimizerBase
    {
	public:
        Adam(float lr = 0.001f, float beta1 = 0.9f, float beta2 = 0.999f);

        virtual void OnStep(vector<ParameterAndGradient>& paramsAndGrads, int batchSize) override;
        virtual OptimizerBase* Clone() const override;
        virtual string ToString() override;
		const char* ClassName() const;

	private:
        float m_LearningRate;
        float m_Beta1;
        float m_Beta2;
        float m_Epsilon = 1e-8f;

        vector<Tensor> m_MGradients;
        vector<Tensor> m_VGradients;
	};
}

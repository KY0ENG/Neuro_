﻿#pragma once
#include "Types.h"

#ifdef CUDA_ENABLED
#include <cuda.h>
#include <cudnn.h>
#include <cublas.h>
#endif

#include "Tensors/TensorOpMultiCpu.h"

namespace Neuro
{
    class TensorOpGpu : public TensorOpMultiCpu
    {
#ifdef CUDA_ENABLED
    public:
        TensorOpGpu();

        virtual void Add(float alpha, const Tensor& t1, float beta, const Tensor& t2, Tensor& result) const override;
        virtual void Mul(bool transposeT1, bool transposeT2, const Tensor& t1, const Tensor& t2, Tensor& result) const override;
        virtual void Transpose(const Tensor& input, Tensor& result) const override;
        virtual void Conv2D(const Tensor& input, const Tensor& kernels, int stride, int paddingX, int paddingY, Tensor& output) const override;
        virtual void Conv2DInputGradient(const Tensor& gradient, const Tensor& kernels, int stride, int paddingX, int paddingY, Tensor& inputGradients) const override;
        virtual void Conv2DKernelsGradient(const Tensor& input, const Tensor& gradient, int stride, int paddingX, int paddingY, Tensor& kernelsGradient) const override;
        virtual void Pool2D(const Tensor& t, int filterSize, int stride, EPoolingMode type, int paddingX, int paddingY, Tensor& result) const override;
        virtual void Pool2DGradient(const Tensor& output, const Tensor& input, const Tensor& outputGradient, int filterSize, int stride, EPoolingMode type, int paddingX, int paddingY, Tensor& result) const override;
        virtual void BatchNormalization(const Tensor& t, const Tensor& gamma, const Tensor& beta, const Tensor& runningMean, const Tensor& runningVar, Tensor& result) const override;
        virtual void BatchNormalizationTrain(const Tensor& t, const Tensor& gamma, const Tensor& beta, float momentum, Tensor& runningMean, Tensor& runningVar, Tensor& saveMean, Tensor& saveInvVariance, Tensor& result) const override;
        virtual void BatchNormalizationGradient(const Tensor& input, const Tensor& gamma, const Tensor& outputGradient, const Tensor& savedMean, const Tensor& savedInvVariance, Tensor& gammaGradient, Tensor& betaGradient, Tensor& inputGradient) const override;
        virtual void Elu(const Tensor& input, float alpha, Tensor& result) const override;
        virtual void EluGradient(const Tensor& output, const Tensor& outputGradient, float alpha, Tensor& result) const override;
        virtual void Softmax(const Tensor& input, Tensor& result) const override;
        virtual void SoftmaxGradient(const Tensor& output, const Tensor& outputGradient, Tensor& result) const override;

    private:
        static cudnnPoolingMode_t GetCudnnPoolType(EPoolingMode type);
        static void GetKernelRunParams(int count, dim3& blocks, dim3& threads);
        static int GetBlocksNum(int count);

        static void CudaAssert(cudaError_t code);
        static void CudaAssert(cudnnStatus_t status);
        static void CudaAssert(cublasStatus_t status);
        static void CudaAssert(const char* error);
        static void CudnnLog(cudnnSeverity_t sev, void *udata, const cudnnDebug_t *dbg, const char *msg);

        static bool s_Initialized;
        static cudaDeviceProp s_CudaDevProp;
        static cublasHandle_t s_CublasHandle;
        static cudnnHandle_t s_CudnnHandle;

#ifdef _DEBUG
#   define CUDA_CHECK(op) CudaAssert(op)
#else
#   define CUDA_CHECK(op) op
#endif
#endif
    };
}
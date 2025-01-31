#pragma once

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <numeric>
#include <iomanip>
#include <limits>
#include <experimental/filesystem>

#undef LoadImage
#include "NeuralStyleTransfer.h"
#include "Memory/MemoryManager.h"
#include "Neuro.h"

#define STYLE "woman_with_hat_matisse"

//#define SLOW
//#define FAST_SINGLE_CONTENT

namespace fs = std::experimental::filesystem;
using namespace Neuro;

class FastNeuralStyleTransfer : public NeuralStyleTransfer
{
public:
    void Run()
    {
        const int NUM_EPOCHS = 2;
        int DETAILS_ITER = 20;
        
#if defined(SLOW)
        const uint32_t IMAGE_WIDTH = 512;
        const uint32_t IMAGE_HEIGHT = 512;
        const float CONTENT_WEIGHT = 1000.f;
        const float STYLE_WEIGHT = 0.1f;
        const float LEARNING_RATE = 2.f;
        const uint32_t BATCH_SIZE = 1;

        const string TEST_FILE = "data/contents/content.jpg";
        //const string TEST_FILE = "e:/!!!/Green_Sea_Turtle_grazing_seagrass.jpg";
#else
        const uint32_t IMAGE_WIDTH = 256;
        const uint32_t IMAGE_HEIGHT = 256;
        const float CONTENT_WEIGHT = 1e3f;
        const float STYLE_WEIGHT = 1e-2f;
        const float LEARNING_RATE = 0.001f;

#       ifdef FAST_SINGLE_CONTENT
        const uint32_t BATCH_SIZE = 1;
#       else
        const uint32_t BATCH_SIZE = 4;
#       endif

        const string TEST_FILE = "data/contents/chicago.jpg";
#endif

        const string STYLE_FILE = string("data/styles/") + STYLE + ".jpg";
        //const string STYLE_FILE = "e:/!!!/The_Great_Wave_off_Kanagawa.jpg";
#ifdef FAST_SINGLE_CONTENT
        const string CONTENT_FILES_DIR = "e:/Downloads/fake_coco";
#else
        const string CONTENT_FILES_DIR = "f:/!TrainingData/coco14";
#endif

        Tensor::SetForcedOpMode(GPU);
        ///GlobalRngSeed(1337);

        auto trainingOn = Tensor({ 1 }, Shape(1), "training_on");
        auto trainingOff = Tensor({ 0 }, Shape(1), "training_off");

        Tensor testImage = LoadImage(TEST_FILE, IMAGE_WIDTH, IMAGE_HEIGHT);
        testImage.SaveAsImage("_test.png", false);

        cout << "Collecting dataset files list...\n";

        vector<string> contentFiles;
        ifstream contentCache = ifstream(CONTENT_FILES_DIR + "_cache");
        if (contentCache)
        {
            string entry;
            while (getline(contentCache, entry))
                contentFiles.push_back(entry);
            contentCache.close();
        }
        else
        {
            auto contentCache = ofstream(CONTENT_FILES_DIR + "_cache");

            // build content files list
            for (const auto& entry : fs::directory_iterator(CONTENT_FILES_DIR))
            {
                contentFiles.push_back(entry.path().generic_string());
                contentCache << contentFiles.back() << endl;
            }

            contentCache.close();
        }
        random_shuffle(contentFiles.begin(), contentFiles.end(), [&](size_t max) { return GlobalRng().Next((int)max); });

        cout << "Creating VGG model...\n";
        
        auto vggModel = VGG16::CreateModel(NCHW, Shape(IMAGE_WIDTH, IMAGE_HEIGHT, 3), false, MaxPool, "data/");
        vggModel->SetTrainable(false);

        cout << "Pre-computing style features and grams...\n";

        vector<TensorLike*> contentOutputs = { vggModel->Layer("block4_conv2")->Outputs()[0] };
        vector<TensorLike*> styleOutputs = { vggModel->Layer("block1_conv1")->Outputs()[0],
                                             vggModel->Layer("block2_conv1")->Outputs()[0],
                                             vggModel->Layer("block3_conv1")->Outputs()[0],
                                             vggModel->Layer("block4_conv1")->Outputs()[0],
                                             vggModel->Layer("block5_conv1")->Outputs()[0],
                                            };

        vector<float> styleOutputsWeights(styleOutputs.size());
        fill(styleOutputsWeights.begin(), styleOutputsWeights.end(), 1.f / styleOutputs.size());

        auto vggFeaturesModel = Flow(vggModel->InputsAt(-1), MergeVectors({ contentOutputs, styleOutputs }), "vgg_features");

        // pre-compute style features of style image (we only need to do it once since that image won't change either)
        Tensor styleImage;
        {
            // style transfer works best when both style and content images have similar resolutions
            float maxDim = (float)max(IMAGE_WIDTH * 2, IMAGE_HEIGHT * 2);
            auto styleDims = GetImageDims(STYLE_FILE);
            float longDim = (float)max(styleDims.Width(), styleDims.Height());
            float scale = maxDim / longDim;
            styleImage = LoadImage(STYLE_FILE, (uint32_t)round(styleDims.Width() * scale), (uint32_t)round(styleDims.Height() * scale));
        }
        ///styleImage.DebugRecoverValues("The_Great_Wave_off_Kanagawa.jpg_raw");
        ///Tensor styleImage = LoadImage(STYLE_FILE);
        styleImage.SaveAsImage("_style.png", false);
        VGG16::PreprocessImage(styleImage, NCHW);
        
        auto styleInput = new Placeholder(styleImage.GetShape(), "style_input");
        auto styleFeaturesNet = vggFeaturesModel(styleInput, "target_style_features");

        auto targetStyleFeatures = Session::Default()->Run(styleFeaturesNet, { { styleInput, &styleImage } });
        targetStyleFeatures.erase(targetStyleFeatures.begin());
        vector<Constant*> targetStyleGrams;
        for (size_t i = 0; i < targetStyleFeatures.size(); ++i)
        {
            // make sure this computation method is in sync with NeuralStyleTransfer::GramMatrix
            Tensor* x = targetStyleFeatures[i];
            uint32_t featureMapSize = x->Width() * x->Height();
            auto features = x->Reshaped(Shape(featureMapSize, x->Depth()));
            targetStyleGrams.push_back(new Constant(features.MatMul(features.Transpose()).Div((float)features.GetShape().Length), "style_" + to_string(i) + "_gram"));
            ///targetStyleGrams.push_back(new Constant(features.Mul(features.Transposed()).Div((float)featureMapSize), "style_" + to_string(i) + "_gram"));
        }

        cout << "Building computational graph...\n";

        // generate final computational graph
        auto input = new Placeholder(Shape(IMAGE_WIDTH, IMAGE_HEIGHT, 3), "input");
        auto inputPre = VGG16::Preprocess(input, NCHW);

#ifdef SLOW
        auto stylizedContent = new Variable(Uniform::Random(-0.5f, 0.5f, input->GetShape()).Add(127.5f), "output_image");
        //auto stylizedContent = new Variable(testImage, "output_image");
        auto stylizedContentPre = VGG16::Preprocess(stylizedContent, NCHW);
#else
        //auto stylizedContent = CreateTransformerNet(inputPre, training);
        auto generator = CreateGeneratorModel(IMAGE_WIDTH, IMAGE_HEIGHT);
        generator->LoadWeights(string(STYLE) + "_weights.h5", false, true);
        auto stylizedContentPre = (*generator)(inputPre)[0];
#endif
        auto stylizedFeatures = vggFeaturesModel(stylizedContentPre, "generated_features");

        // compute content loss from first output...
        auto targetContentFeatures = vggFeaturesModel(inputPre, "target_content_features")[0];
        auto contentLoss = ContentLoss(targetContentFeatures, stylizedFeatures[0]);
        auto weightedContentLoss = multiply(contentLoss, CONTENT_WEIGHT);
        stylizedFeatures.erase(stylizedFeatures.begin());

        vector<TensorLike*> styleLosses;
        // ... and style losses from remaining outputs
        assert(stylizedFeatures.size() == targetStyleGrams.size());
        for (size_t i = 0; i < stylizedFeatures.size(); ++i)
            styleLosses.push_back(multiply(StyleLoss(targetStyleGrams[i], stylizedFeatures[i], (int)i), styleOutputsWeights[i]));
        auto weightedStyleLoss = multiply(merge_sum(styleLosses, "mean_style_loss"), STYLE_WEIGHT, "style_loss");

        auto totalLoss = div(add(weightedContentLoss, weightedStyleLoss), (float)BATCH_SIZE, "total_loss");

        auto optimizer = Adam(LEARNING_RATE);
        auto minimize = optimizer.Minimize({ totalLoss });

        Tensor contentBatch(Shape::From(input->GetShape(), BATCH_SIZE));

#if defined(SLOW) || defined(FAST_SINGLE_CONTENT)
        size_t steps = 20000;
#else
        size_t steps = 160000;
#endif

        float minLoss = 0;
        float lastLoss = 0;

        ImageLoader contentLoader(contentFiles, BATCH_SIZE, 2);
        DataPreloader preloader({ input->OutputPtr() }, { &contentLoader }, 4);

        Tqdm progress(steps, 0);
        progress.ShowStep(true).ShowPercent(false).ShowElapsed(false).ShowIterTime(true);// .EnableSeparateLines(true);
        for (int i = 0; i < steps; ++i, progress.NextStep())
        {
#if defined(SLOW) || defined(FAST_SINGLE_CONTENT)
            testImage.CopyTo(contentBatch);
#else
            preloader.Load();
#endif
                
            auto results = Session::Default()->Run(MergeVectors({ vector<TensorLike*>{ stylizedContentPre, totalLoss, weightedContentLoss, weightedStyleLoss, minimize }, styleLosses }),
                                                    { /*{ input, &contentBatch },*/ });

            if (i % DETAILS_ITER == 0)
            {
#if !defined(SLOW) && !defined(FAST_SINGLE_CONTENT)
                auto results = Session::Default()->Run({ stylizedContentPre, totalLoss, weightedContentLoss, weightedStyleLoss },
                                                        { { input, &testImage } });
#endif

                float loss = (*results[1])(0);
                auto genImage = *results[0];
                VGG16::DeprocessImage(genImage, NCHW);
                genImage.SaveAsImage(string(STYLE) + "_" + to_string(i) + "_output.png", false);
                if (minLoss <= 0 || loss < minLoss)
                {
#if !defined(SLOW)
                    generator->SaveWeights(string(STYLE) + "_weights.h5");
#endif
                    minLoss = loss;
                }

                const float SINGLE_STYLE_WEIGHT = STYLE_WEIGHT / styleLosses.size();

                float change = 0;
                if (lastLoss > 0)
                    change = (lastLoss - loss) / lastLoss * 100.f;
                lastLoss = loss;

                cout << endl;
                cout << setprecision(4) << "iter: " << i << " - total loss: " << loss << "(min: " << minLoss << ") - change: " << change << "%"<< endl;
                cout << "----------------------------------------------------" << endl;
                cout << "content loss: " << (*results[2])(0) << " - style loss: " << (*results[3])(0) << endl;
#if defined(SLOW) || defined(FAST_SINGLE_CONTENT)
                cout << "style_1 loss: " << (*results[5])(0) * SINGLE_STYLE_WEIGHT; if (styleLosses.size() > 1) cout << " - style_2 loss: " << (*results[6])(0) * SINGLE_STYLE_WEIGHT; cout << endl;
                if (styleLosses.size() > 2) cout << "style_3 loss: " << (*results[7])(0) * SINGLE_STYLE_WEIGHT; if (styleLosses.size() > 3) cout << " - style_4 loss: " << (*results[8])(0) * SINGLE_STYLE_WEIGHT; if (styleLosses.size() > 2) cout << endl;
                if (styleLosses.size() > 4) cout << "style_5 loss: " << (*results[9])(0) * SINGLE_STYLE_WEIGHT; if (styleLosses.size() > 4) cout << endl;
#endif
                cout << "----------------------------------------------------" << endl;
            }
        }
    }

    void Test()
    {
        Tensor::SetForcedOpMode(GPU);
        Tensor testContent(Shape(320, 320, 3));
        SampleImagesBatch(LoadFilesList("e:/Downloads/test_content", false), testContent, false);
        testContent.SaveAsImage("_test_content.png", false);

        /*const string TEST_FILE = "data/contents/content.jpg";
        Tensor testImage = LoadImage(TEST_FILE);*/
        auto input = new Placeholder(testContent.GetShape(), "input");
        auto inputPre = VGG16::Preprocess(input, NCHW);
        auto generator = CreateGeneratorModel(testContent.GetShape().Width(), testContent.GetShape().Height());
        generator->LoadWeights(string("data/") + STYLE + "_weights.h5", false, true);
        auto stylizedContentPre = (*generator)(inputPre)[0];

        auto results = Session::Default()->Run({ stylizedContentPre }, { { input, &testContent } });
        auto genImage = *results[0];
        VGG16::DeprocessImage(genImage, NCHW);
        genImage.SaveAsImage(string(STYLE) + "_test_output.png", false);
    }

    TensorLike* CreateTransformerNet(TensorLike* input);

    ModelBase* CreateGeneratorModel(uint32_t width, uint32_t height);

    class OutputScale : public LayerBase
    {
    public:
        OutputScale(const string& name = "") : LayerBase(__FUNCTION__, Shape(), name) {}
    protected:
        virtual vector<TensorLike*> InternalCall(const vector<TensorLike*>& inputNodes) override { return { multiply(inputNodes[0], 150.f) }; }
    };
};

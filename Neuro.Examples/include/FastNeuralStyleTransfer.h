#pragma once

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <numeric>
#include <iomanip>
#include <experimental/filesystem>

#undef LoadImage

#include "NeuralStyleTransfer.h"
#include "Memory/MemoryManager.h"
#include "Neuro.h"
#include "VGG19.h"

#define SLOW
#define FAST_SINGLE_CONTENT

namespace fs = std::experimental::filesystem;
using namespace Neuro;

class FastNeuralStyleTransfer : public NeuralStyleTransfer
{
public:
    void Run()
    {
        
        const int NUM_EPOCHS = 20;
        
#ifdef SLOW
        const uint32_t IMAGE_WIDTH = 512;
        const uint32_t IMAGE_HEIGHT = 512;
        const float CONTENT_WEIGHT = 7.5f;
        const float STYLE_WEIGHT = 0.0001f;
        const float LEARNING_RATE = 1.f;
        const uint32_t BATCH_SIZE = 1;
#else
        const uint32_t IMAGE_WIDTH = 256;
        const uint32_t IMAGE_HEIGHT = 256;
        const float CONTENT_WEIGHT = 7.5f;
        const float STYLE_WEIGHT = 100.f;
        const float LEARNING_RATE = 0.001f;

#       ifdef FAST_SINGLE_CONTENT
        const uint32_t BATCH_SIZE = 1;
#       else
        const uint32_t BATCH_SIZE = 4;
#       endif
#endif

        const string STYLE_FILE = "data/style.jpg";
        const string TEST_FILE = "data/content.jpg";
#       ifdef FAST_SINGLE_CONTENT
        const string CONTENT_FILES_DIR = "e:/Downloads/fake_coco";
#       else
        const string CONTENT_FILES_DIR = "e:/Downloads/coco14";
#       endif

        Tensor::SetForcedOpMode(GPU);
        GlobalRngSeed(1337);

        Tensor testImage = LoadImage(TEST_FILE, IMAGE_WIDTH, IMAGE_HEIGHT, NCHW);
        testImage.SaveAsImage("_test.jpg", false);

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

        cout << "Creating VGG model...\n";
        
        auto vggModel = VGG16::CreateModel(NCHW, Shape(IMAGE_WIDTH, IMAGE_HEIGHT, 3), false);
        vggModel->SetTrainable(false);

        cout << "Pre-computing style features and grams...\n";

        vector<TensorLike*> contentOutputs = { vggModel->Layer("block4_conv2")->Outputs()[0] };
        vector<TensorLike*> styleOutputs = { vggModel->Layer("block1_conv2")->Outputs()[0],
                                             //vggModel->Layer("block2_conv1")->Outputs()[0],
                                             //vggModel->Layer("block3_conv1")->Outputs()[0],
                                             //vggModel->Layer("block4_conv1")->Outputs()[0],
                                             //vggModel->Layer("block5_conv1")->Outputs()[0] 
                                            };

        auto vggFeaturesModel = Flow(vggModel->InputsAt(-1), MergeVectors({ contentOutputs, styleOutputs }), "vgg_features");

        // pre-compute style features of style image (we only need to do it once since that image won't change either)
        Tensor styleImage = LoadImage(STYLE_FILE);
        styleImage.SaveAsImage("_style.jpg", false);
        VGG16::PreprocessImage(styleImage, NCHW);

        auto styleInput = new Placeholder(styleImage.GetShape(), "style_input");
        auto styleFeaturesNet = vggFeaturesModel(styleInput);

        auto targetStyleFeatures = Session::Default()->Run(styleFeaturesNet, { { styleInput, &styleImage } });
        targetStyleFeatures.erase(targetStyleFeatures.begin());
        vector<Constant*> targetStyleGrams;
        for (size_t i = 0; i < targetStyleFeatures.size(); ++i)
        {
            Tensor* x = targetStyleFeatures[i];
            uint32_t featureMapSize = x->Width() * x->Height();
            auto features = x->Reshaped(Shape(featureMapSize, x->Depth()));
            targetStyleGrams.push_back(new Constant(features.Mul(features.Transposed()).Div((float)featureMapSize), "style_" + to_string(i) + "_gram"));
        }

        cout << "Building computational graph...\n";

        // generate final computational graph
        auto training = new Placeholder(Shape(1), "training");
        auto input = new Placeholder(Shape(IMAGE_WIDTH, IMAGE_HEIGHT, 3), "input");
        auto inputPre = VGG16::Preprocess(input, NCHW);

        auto targetContentFeatures = vggFeaturesModel(inputPre)[0];

#ifdef SLOW
        auto stylizedContent = new Variable(Uniform::Random(-0.5f, 0.5f, input->GetShape()).Add(127.5f), "output_image");
#else
        auto stylizedContent = CreateTransformerNet(divide(input, 255.f), training);
#endif

        auto stylizedContentPre = VGG16::Preprocess(stylizedContent, NCHW);
        auto stylizedFeatures = vggFeaturesModel(stylizedContentPre);

        // compute content loss from first output...
        auto contentLoss = ContentLoss(targetContentFeatures, stylizedFeatures[0]);
        auto weightedContentLoss = multiply(contentLoss, CONTENT_WEIGHT);
        stylizedFeatures.erase(stylizedFeatures.begin());

        vector<TensorLike*> styleLosses;
        // ... and style losses from remaining outputs
        assert(stylizedFeatures.size() == targetStyleGrams.size());
        for (size_t i = 0; i < stylizedFeatures.size(); ++i)
            styleLosses.push_back(StyleLoss(targetStyleGrams[i], stylizedFeatures[i], (int)i));
        auto weightedStyleLoss = multiply(merge_sum(styleLosses, "mean_style_loss"), STYLE_WEIGHT, "style_loss");

        //auto totalLoss = add(weightedContentLoss, weightedStyleLoss, "total_loss");
        //auto totalLoss = weightedContentLoss;
        auto totalLoss = weightedStyleLoss;
        //auto totalLoss = mean(square(sub(stylizedContentPre, contentPre)), GlobalAxis, "total");

        auto optimizer = Adam(LEARNING_RATE);
        auto minimize = optimizer.Minimize({ totalLoss });

        Tensor contentBatch(Shape::From(input->GetShape(), BATCH_SIZE));

#if defined(SLOW) || defined(FAST_SINGLE_CONTENT)
        size_t steps = 500;
#else
        size_t samples = contentFiles.size();
        size_t steps = samples / BATCH_SIZE;
#endif

        //Debug::LogAllOutputs(true);
        //Debug::LogAllGrads(true);
        /*Debug::LogOutput("vgg_preprocess", true);
        Debug::LogOutput("output_image", true);
        Debug::LogGrad("vgg_preprocess", true);
        Debug::LogGrad("output_image", true);*/

        auto trainingOn = Tensor({ 1 }, Shape(1), "training_on");
        auto trainingOff = Tensor({ 0 }, Shape(1), "training_off");

        for (int e = 0; e < NUM_EPOCHS; ++e)
        {
            cout << "Epoch " << e+1 << endl;

            Tqdm progress(steps, 0);
            progress.ShowStep(true).ShowPercent(false).ShowElapsed(false).EnableSeparateLines(true);
            for (int i = 0; i < steps; ++i, progress.NextStep())
            {
                contentBatch.OverrideHost();
#ifdef SLOW
                testImage.CopyTo(contentBatch);
#else
                for (int j = 0; j < BATCH_SIZE; ++j)
                    LoadImage(contentFiles[(i * BATCH_SIZE + j)%contentFiles.size()], contentBatch.Values() + j * contentBatch.BatchLength(), input->GetShape().Width(), input->GetShape().Height(), NCHW);
#endif
                //VGG16::PreprocessImage(contentBatch, NCHW);
                //contentBatch.SaveAsImage("batch" + to_string(i) + ".jpg", false);
                //auto contentFeatures = *vggFeaturesModel.Eval(contentOutputs, { { (Placeholder*)(vggFeaturesModel.InputsAt(0)[0]), &contentBatch } })[0];

                /*auto results = Session::Default()->Run({ stylizedContent, contentLoss, styleLosses[0], styleLosses[1], styleLosses[2], styleLosses[3], totalLoss, minimize }, 
                                                       { { content, &contentBatch }, { training, &trainingOn } });
                
                uint64_t cLoss = (uint64_t)(*results[1])(0);
                uint64_t sLoss1 = (uint64_t)(*results[2])(0);
                uint64_t sLoss2 = (uint64_t)(*results[3])(0);
                uint64_t sLoss3 = (uint64_t)(*results[4])(0);
                uint64_t sLoss4 = (uint64_t)(*results[5])(0);
                stringstream extString;
                extString << setprecision(4) << " - cL: " << cLoss << " - s1L: " << sLoss1 << " - s2L: " << sLoss2 <<
                    " - s3L: " << sLoss3 << " - s4L: " << sLoss4 << " - tL: " << (cLoss + sLoss1 + sLoss2 + sLoss3 + sLoss4);
                progress.SetExtraString(extString.str());*/

                auto results = Session::Default()->Run({ stylizedContent, totalLoss, minimize },
                    { { input, &contentBatch },{ training, &trainingOn } });

                stringstream extString;
                extString << setprecision(4) << " - total_l: " << (*results[1])(0);
                progress.SetExtraString(extString.str());

                /*auto results = Session::Default()->Run({ stylizedContent, weightedContentLoss, weightedStyleLoss, totalLoss, minimize },
                                                       { { input, &contentBatch }, { training, &trainingOn } });

                stringstream extString;
                extString << setprecision(4) << " - content_l: " << (*results[1])(0) << " - style_l: " << (*results[2])(0) << " - total_l: " << (*results[3])(0);
                progress.SetExtraString(extString.str());*/

                if (i % 10 == 0)
                {
                    auto genImage = *results[0];
                    //VGG16::UnprocessImage(genImage, NCHW);
                    genImage.Clipped(0, 255, genImage);
                    genImage.NormalizedMinMax(GlobalAxis, 0, 255).SaveAsImage("fnst_e" + PadLeft(to_string(e), 4, '0') + "_b" + PadLeft(to_string(i), 4, '0') + ".png", false);
                    /*auto result = Session::Default()->Run({ stylizedContent, weightedContentLoss, weightedStyleLoss }, { { content, &testImage }, { training, &trainingOff } });
                    cout << "test content_loss: " << (*result[1])(0) << " style_loss: " << (*result[2])(0) << endl;
                    auto genImage = *result[0];
                    VGG16::UnprocessImage(genImage, NCHW);
                    genImage.SaveAsImage("fnst_e" + PadLeft(to_string(e), 4, '0') + "_b" + PadLeft(to_string(i), 4, '0') + ".png", false);*/
                }
            }
        }

        /*{
            auto result = Session::Default()->Run({ stylizedContent, weightedContentLoss, weightedStyleLoss }, { { content, &testImage }, { training, &trainingOff } });
            cout << "test content_loss: " << (*result[1])(0) << " style_loss: " << (*result[2])(0) << endl;
            auto genImage = *result[0];
            VGG16::UnprocessImage(genImage, NCHW);
            genImage.SaveAsImage("fnst.png", false);
        }*/

        //auto results = Session::Default()->Run({ outputImg }, {});
        //auto genImage = *results[0];
        //VGG16::UnprocessImage(genImage, NCHW);
        //genImage.SaveAsImage("_neural_transfer.jpg", false);
    }

    TensorLike* CreateTransformerNet(TensorLike* input, TensorLike* training);
};
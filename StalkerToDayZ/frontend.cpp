#include "stdafx.h"
#include "myNuklear.h"
#include "globals.h"
#include "GLIPLib.hpp"
#include "GLFW/glfw3.h"
#define  IL_STATIC_LIB
#include "IL/il.h"
#include "resource.h"
#pragma comment(lib, "GLIP-Lib")

namespace
{
//
enum
{
    ERR_NO_DDS,
    ERR_MISSED_DDS,
    ERR_NO_ALPHA,
    READY,
    WORKING,
    DONE
} uiState;
struct SBaseTextureInfo
{
    std::string rootName;
    std::string textureBase;
};
struct SBumpTexturesInfo
{
    std::string rootName;
    std::string textureBump;
    std::string textureBumpSharp;
};
std::vector<SBaseTextureInfo>  baseTextures;
std::vector<SBumpTexturesInfo> bumpTexturesSets;
std::vector<std::string>  missedTextures;
std::string textureWeAreCurrentlyWorkingWith;
size_t      numOfTexturesAreFinished=0u;
std::mutex  mutex; //for textureWeAreCurrentlyWorkingWith
std::string noAlphaTextureNameError;
int         isSavingInSameFolder     = 0;
int         isAutoDeletingInputFiles = 0;
int         isKeepingAlphaInBase     = 0;

size_t  TotalNumOfTextures()
{
    return baseTextures.size() + bumpTexturesSets.size() * 2;
}

void StartProcessingTextures()
{
    constexpr auto& outputFolderStr = "export";
    if (!isSavingInSameFolder)
    {
        std::filesystem::create_directories(outputFolderStr);

    }
    
    ilInit();
    ilEnable(IL_FILE_OVERWRITE);
    
    glfwInit();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(1, 1, "", nullptr, nullptr);
    glfwMakeContextCurrent(window);

    Glip::HandleOpenGL::init();
    HMODULE     handle = GetModuleHandle(NULL);
    HRSRC       rc;
    HGLOBAL     rcData;
    DWORD       size;
    const char* data;

    auto loadShader = [&](const int shaderID)
    {
        std::string result;
        HMODULE handle = GetModuleHandle(NULL);
        HRSRC   rc = FindResource(handle, MAKEINTRESOURCE(shaderID), MAKEINTRESOURCE(SHADER_TYPE_ID));
        HGLOBAL rcData = LoadResource(handle, rc);
        DWORD   size = SizeofResource(handle, rc);
        auto* data = static_cast<const char*>(::LockResource(rcData));
        result.assign(data, size);
        return result;
    };

    Glip::CoreGL::ShaderSource nohqShader(loadShader(SHADER_NOHQ_ID));
    Glip::CoreGL::ShaderSource smdiShader(loadShader(SHADER_SMDI_ID));

    auto preparePathForOutputTexture = [&](const std::string& rootName, const char* const suffix)
    {
        std::string result;
        if (!isSavingInSameFolder)
        {
            result.append(outputFolderStr).append("//");
        }
        result.append(rootName).append(suffix);
        return result;
    };

    auto removeSourceTextureIfNeeded = [&](const std::string& textureToDelete)
    {
        if (isAutoDeletingInputFiles)
        {
            std::remove(textureToDelete.c_str());
        }
    };

    
    enum
    {
        BASE = 0u,
        BUMP,
        BUMP_SHARP,
        OUTPUT
    };
    for (const auto& baseTextureInfo : baseTextures)
    {
        //_CO OR _CA TEXTURE
        ilBindImage(BASE);
        ilLoadImage(baseTextureInfo.textureBase.c_str());

        const bool baseTextureIsRGBA           = ilGetInteger(IL_IMAGE_FORMAT) == IL_RGBA;
        const bool exportTextureShouldBeRGBA   = isKeepingAlphaInBase && baseTextureIsRGBA;
        const std::string pathForOutputTexture = preparePathForOutputTexture(baseTextureInfo.rootName, exportTextureShouldBeRGBA?"_ca.png":"_co.png");

        {
            std::lock_guard guard(mutex);
            textureWeAreCurrentlyWorkingWith = pathForOutputTexture;
        }

        if (!exportTextureShouldBeRGBA && baseTextureIsRGBA)
        {
            ilConvertImage(IL_RGB, IL_UNSIGNED_BYTE); //remove alpha
        }
        ilSaveImage(pathForOutputTexture.c_str());
        removeSourceTextureIfNeeded(baseTextureInfo.textureBase);
        numOfTexturesAreFinished++;
    }
    bool isNoAlphaErrorHappened = false;
    for (const auto& bumpTexturesInfo : bumpTexturesSets)
    {
        {//_NOHQ TEXTURE
            const std::string pathForOutputTexture = preparePathForOutputTexture(bumpTexturesInfo.rootName, "_nohq.png");
            {
                std::lock_guard guard(mutex);
                textureWeAreCurrentlyWorkingWith = pathForOutputTexture;
            }
            ilBindImage(BUMP);
            ilLoadImage(bumpTexturesInfo.textureBump.c_str());
            const int imgWidth     = ilGetInteger(IL_IMAGE_WIDTH);
            const int imgHeight    = ilGetInteger(IL_IMAGE_HEIGHT);
            if (ilGetInteger(IL_IMAGE_FORMAT) != IL_RGBA)
            {
                noAlphaTextureNameError = bumpTexturesInfo.textureBump;
                isNoAlphaErrorHappened = true;
                break;
            }

            Glip::CoreGL::HdlTextureFormat     formatBump(imgWidth, imgHeight, GL_RGBA, GL_UNSIGNED_BYTE);
            Glip::CoreGL::HdlTextureFormat     formatOutput(imgWidth, imgHeight, GL_RGB, GL_UNSIGNED_BYTE);
            Glip::CorePipeline::FilterLayout   filterLayout("fl", formatOutput, nohqShader);
            Glip::CorePipeline::PipelineLayout pipelineLayout("pl");
            pipelineLayout.addInput("bumpTexture");
            pipelineLayout.addOutput("outputTexture");
            const int filterId = pipelineLayout.add(filterLayout, "fn");
            pipelineLayout.autoConnect();
            Glip::Pipeline pipeline(pipelineLayout, "pn");
            pipeline[filterId].program().setVar("textureSize", GL_FLOAT_VEC2, static_cast<float>(imgWidth), static_cast<float>(imgHeight));

            Glip::HdlTexture bumpTexture(formatBump);
            bumpTexture.write(ilGetData(), GL_RGBA);
            pipeline << bumpTexture << Glip::Pipeline::Process;
            ilBindImage(OUTPUT);
            ilTexImage(imgWidth, imgHeight, 0, 3, IL_RGB, IL_UNSIGNED_BYTE, nullptr);
            pipeline.out(0).read(ilGetData());
            ilSaveImage(pathForOutputTexture.c_str());
            numOfTexturesAreFinished++;
        }
        {//_SMDI TEXTURE
            const std::string pathForOutputTexture = preparePathForOutputTexture(bumpTexturesInfo.rootName, "_smdi.png");
            {
                std::lock_guard guard(mutex);
                textureWeAreCurrentlyWorkingWith = pathForOutputTexture;
            }
            ilBindImage(BUMP_SHARP);
            ilLoadImage(bumpTexturesInfo.textureBumpSharp.c_str());
            if (ilGetInteger(IL_IMAGE_FORMAT) != IL_RGBA)
            {
                noAlphaTextureNameError = bumpTexturesInfo.textureBumpSharp;
                isNoAlphaErrorHappened = true;
                break;
            }
            Glip::CoreGL::HdlTextureFormat     formatBumpSharp(ilGetInteger(IL_IMAGE_WIDTH), ilGetInteger(IL_IMAGE_HEIGHT), GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, GL_LINEAR);
            ilBindImage(BUMP);
            const int imgWidth  = ilGetInteger(IL_IMAGE_WIDTH);
            const int imgHeight = ilGetInteger(IL_IMAGE_HEIGHT);

            Glip::CoreGL::HdlTextureFormat     formatBump(imgWidth, imgHeight, GL_RGBA, GL_UNSIGNED_BYTE);
            Glip::CoreGL::HdlTextureFormat     formatOutput(imgWidth, imgHeight, GL_RGB, GL_UNSIGNED_BYTE);
            Glip::CorePipeline::FilterLayout   filterLayout("fl", formatOutput, smdiShader);
            Glip::CorePipeline::PipelineLayout pipelineLayout("pl");
            pipelineLayout.addInput("bumpTexture");
            pipelineLayout.addInput("bumpSharpTexture");
            pipelineLayout.addOutput("outputTexture");
            const int filterId = pipelineLayout.add(filterLayout, "fn");
            pipelineLayout.autoConnect();
            Glip::Pipeline pipeline(pipelineLayout, "pn");
            pipeline[filterId].program().setVar("textureSize", GL_FLOAT_VEC2, static_cast<float>(imgWidth), static_cast<float>(imgHeight));

            Glip::HdlTexture bumpTexture(formatBump);
            bumpTexture.write(ilGetData(), GL_RGBA);
            ilBindImage(BUMP_SHARP);
            Glip::HdlTexture bumpSharpTexture(formatBumpSharp);
            bumpSharpTexture.write(ilGetData(), GL_RGBA);

            pipeline << bumpTexture << bumpSharpTexture << Glip::Pipeline::Process;
            ilBindImage(OUTPUT);
            ilTexImage(imgWidth, imgHeight, 0, 3, IL_RGB, IL_UNSIGNED_BYTE, nullptr);
            pipeline.out(0).read(ilGetData());
            ilSaveImage(pathForOutputTexture.c_str());
            removeSourceTextureIfNeeded(bumpTexturesInfo.textureBump);
            removeSourceTextureIfNeeded(bumpTexturesInfo.textureBumpSharp);
            numOfTexturesAreFinished++;
        }
    }
    Glip::HandleOpenGL::deinit();
    glfwTerminate();
    uiState = isNoAlphaErrorHappened ? ERR_NO_ALPHA : DONE;
}
//
}


void OnStart()
{
    //Iterate textures in folder
    constexpr auto& strDDS = ".dds";
    std::vector<std::string> allDDS;
    for (const auto& item : std::filesystem::directory_iterator("."))
    {
        if (item.is_regular_file())
        {
            std::string filename = item.path().filename().string();
            if (filename.find(strDDS, filename.size() - sizeof(strDDS)+1)!=std::string::npos) //collect all .dds textures
            {
                allDDS.emplace_back(std::move(filename));
            }
        }
    }
    
    while (!allDDS.empty())
    {
        std::string texture(std::move(allDDS.back()));
        allDDS.pop_back();
        constexpr auto& strBump      = "_bump.dds";
        constexpr auto& strBumpSharp = "_bump#.dds";

        auto AddToSetOrAddToMissed = [&](const std::string& textureName, std::string& targetStringInSet)
        {
            auto iterator = std::find(allDDS.begin(), allDDS.end(), textureName);
            if (iterator == allDDS.end())
            {
                missedTextures.emplace_back(textureName);
            }
            else
            {
                targetStringInSet = std::move(*iterator);
                iterator->swap(allDDS.back());
                allDDS.pop_back();
            }
        };

        if (size_t offset = texture.size() - sizeof(strBump) + 1; texture.find(strBump, offset) != std::string::npos) //Bump texture
        {
            auto& currentSet             = bumpTexturesSets.emplace_back();
            std::string baseTexture      = texture.substr(0, offset);
            std::string bumpSharpTexture = baseTexture;
            currentSet.rootName          = baseTexture;
            baseTexture.append(strDDS);
            bumpSharpTexture.append(strBumpSharp);
            currentSet.textureBump = std::move(texture);
            AddToSetOrAddToMissed(bumpSharpTexture, currentSet.textureBumpSharp);
        }
        else if(offset = texture.size() - sizeof(strBumpSharp) + 1; texture.find(strBumpSharp, offset) != std::string::npos) //Bump Sharp texture
        {
            auto& currentSet = bumpTexturesSets.emplace_back();
            std::string baseTexture = texture.substr(0, offset);
            std::string bumpTexture = baseTexture;
            currentSet.rootName     = baseTexture;
            baseTexture.append(strDDS);
            bumpTexture.append(strBump);
            currentSet.textureBumpSharp = std::move(texture);
            AddToSetOrAddToMissed(bumpTexture, currentSet.textureBump);
        }
        else //Base texture
        {
            offset = texture.size() - sizeof(strDDS) + 1;

            auto& baseTexture       = baseTextures.emplace_back();
            baseTexture.rootName    = texture.substr(0, offset);
            baseTexture.textureBase = std::move(texture);
        }
    }

    if (baseTextures.empty() && bumpTexturesSets.empty())
    {
        uiState = ERR_NO_DDS;
        wndHeight = 82;
    }
    else if (missedTextures.empty())
    {
        uiState = READY;
    }
    else
    {
        uiState = ERR_MISSED_DDS;
        wndHeight = 286;
    }

}

void Finish()
{

}

void Frame()
{
    auto drawCloseBtn = []
    {
        nk_layout_row_dynamic(ctx, 0, 1);
        if (nk_button_label(ctx, "Закрыть"))
        {
            isRunning = false;
        }
    };
    
    auto drawStartBtn = []
    {
        nk_layout_row_dynamic(ctx, 0, 1);
        if (nk_button_label(ctx, "Старт"))
        {
            uiState = WORKING;
            std::thread thread(StartProcessingTextures);
            thread.detach();
            wndHeight = 82;
        }
    };

    switch (uiState)
    {
    case ERR_NO_DDS:
        {
            nk_layout_row_dynamic(ctx, 15, 1);
            nk_label(ctx, "В папке отсуствуют dds-текстуры", NK_TEXT_CENTERED);
            drawCloseBtn();
            break;
        }
    case ERR_MISSED_DDS:
        {
            static const std::string missedTexturesAsString = [&]
            {
                std::string result;
                for (const auto& textureName : missedTextures)
                {
                    result.append(textureName).append("\n");
                }
                return result;
            }();
            nk_layout_row_dynamic(ctx, 15, 1);
            nk_label(ctx, " В папке не хватает следующих текстур:", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 200, 1);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_EDITOR, (char*)missedTexturesAsString.data(), missedTexturesAsString.size(), nullptr);
            drawCloseBtn();
        }
        break;
    case ERR_NO_ALPHA:
    {
        static const std::string errMessage = [&]
        {
            std::string result;
            result.append("Ошибка. В текстуре ").append(noAlphaTextureNameError).append(" отсуствует альфа-канал");
            return result;
        }();
        nk_layout_row_dynamic(ctx, 15, 1);
        nk_label(ctx, errMessage.c_str(), NK_TEXT_CENTERED);
        drawCloseBtn();
        break;
    }
    case READY:
        {
            static const std::string readyToGoMessage = [&]
            {
                std::string result;
                result.append("Всего текстур для конвертирования: ").append(std::to_string(TotalNumOfTextures()));
                return result;
            }();
            nk_layout_row_dynamic(ctx, 15, 1);
            nk_label(ctx, readyToGoMessage.c_str(), NK_TEXT_CENTERED);
            nk_layout_row_dynamic(ctx, 15, 1);
            nk_checkbox_label(ctx, "Сохранять в исходной папке", &isSavingInSameFolder);
            nk_checkbox_label(ctx, "Автоудаление исходников", &isAutoDeletingInputFiles);
            nk_checkbox_label(ctx, "Не удалять прозрачность диффузной текстуры", &isKeepingAlphaInBase);
            drawStartBtn();
        }
        break;
    case WORKING:
        {
            static std::string holder;
            {
                std::lock_guard guard(mutex);
                if (!textureWeAreCurrentlyWorkingWith.empty())
                {
                    holder = std::move(textureWeAreCurrentlyWorkingWith);
                }
            }
            static size_t texturesTotal = TotalNumOfTextures();
            nk_layout_row_dynamic(ctx, 15, 1);
            nk_label(ctx, holder.c_str(), NK_TEXT_CENTERED);
            nk_layout_row_dynamic(ctx, 0, 1);
            nk_progress(ctx, &numOfTexturesAreFinished, texturesTotal, false);
        }
        break;
    case DONE:
        {
            nk_layout_row_dynamic(ctx, 15, 1);
            nk_label(ctx, "Работа закончена", NK_TEXT_CENTERED);
            drawCloseBtn();
        }
        break;
    }
}



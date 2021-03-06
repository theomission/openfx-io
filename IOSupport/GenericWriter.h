/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-io <https://github.com/MrKepzie/openfx-io>,
 * Copyright (C) 2015 INRIA
 *
 * openfx-io is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-io is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-io.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX GenericWriter plugin.
 * A base class for all OpenFX-based encoders.
 */

#ifndef Io_GenericWriter_h
#define Io_GenericWriter_h

#include <memory>
#include <ofxsImageEffect.h>
#include "IOUtility.h"
#include "ofxsMacros.h"
#include "ofxsPixelProcessor.h" // for getImageData
#include "ofxsCopier.h" // for copyPixels

namespace OFX {
    class PixelProcessorFilterBase;
}
class GenericOCIO;

enum LayerViewsPartsEnum
{
    eLayerViewsSinglePart = 0,
    eLayerViewsSplitViews,
    eLayerViewsSplitViewsLayers
};

/**
 * @brief A generic writer plugin, derive this to create a new writer for a specific file format.
 * This class propose to handle the common stuff among writers:
 * - common params
 * - a way to inform the host about the colour-space of the data.
 **/
class GenericWriterPlugin : public OFX::ImageEffect {
    
public:
    
    GenericWriterPlugin(OfxImageEffectHandle handle);
    
    virtual ~GenericWriterPlugin();
    
    /**
     * @brief Don't override this function, the GenericWriterPlugin class already does the rendering. The "encoding" of the frame
     * must be done by the pure virtual function encode(...) instead.
     * The render function also copies the image from the input clip to the output clip (only if the effect is connected downstream)
     * in order to be able to branch this effect in the middle of an effect tree.
     **/
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE;

    /** @brief client begin sequence render function */
    virtual void beginSequenceRender(const OFX::BeginSequenceRenderArguments &args) OVERRIDE;
    
    /** @brief client end sequence render function */
    virtual void endSequenceRender(const OFX::EndSequenceRenderArguments &args) OVERRIDE;
    
    /**
     * @brief Don't override this. It returns the projects region of definition.
     **/
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /**
     * @brief Don't override this. It returns the source region of definition.
     **/
    //virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    /**
     * @brief Don't override this. It returns the frame range to render.
     **/
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

    /**
     * @brief You can override this to take actions in response to a param change.
     * Make sure you call the base-class version of this function at the end: i.e:
     *
     * void MyReader::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
     *      if (.....) {
     *
     *      } else if(.....) {
     *
     *      } else {
     *          GenericReaderPlugin::changedParam(args,paramName);
     *      }
     * }
     **/
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE;
    
    /**
     * @brief Overriden to handle premultiplication parameter given the input clip.
     * Make sure you call the base class implementation if you override it.
     **/
    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE;
    
    /**
     * @brief Overriden to set the clips premultiplication according to the user and plug-ins wishes.
     **/
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE;
    
    /**
     * @brief Overriden to request the views needed to render.
     **/
    virtual void getFrameViewsNeeded(const OFX::FrameViewsNeededArguments& args, OFX::FrameViewsNeededSetter& frameViews) OVERRIDE FINAL;
    
    /**
     * @brief Overriden to clear any OCIO cache.
     * This function calls clearAnyCache() if you have any cache to clear.
     **/
    void purgeCaches(void) OVERRIDE FINAL;

    
protected:
    
    
    
    /**
     * @brief Override this function to actually encode the image in the file pointed to by filename.
     * If the file is a video-stream then you should encode the frame at the time given in parameters.
     * You must write the decoded image into dstImg. This function should convert the  pixels from srcImg
     * into the color-space and bitdepths of the newly created images's file.
     * You can inform the host of the bitdepth you support in input in the describe() function.
     * You can always skip the color-space conversion, but for all linear hosts it would produce either
     * false colors or sub-par performances in the case the end-user has to prepend a color-space conversion
     * effect her/himself.
     *
     * @pre The filename has been validated against the supported file extensions.
     * You don't need to check this yourself.
     **/
    virtual void encode(const std::string& filename,
                        OfxTime time,
                        const std::string& viewName, 
                        const float *pixelData,
                        const OfxRectI& bounds,
                        float pixelAspectRatio,
                        OFX::PixelComponentEnum pixelComponents,
                        int rowBytes);
    
    
    virtual void beginEncode(const std::string& /*filename*/,
                             const OfxRectI& /*rodPixel*/,
                             float /*pixelAspectRatio*/,
                             const OFX::BeginSequenceRenderArguments &/*args*/) {}
    
    virtual void endEncode(const OFX::EndSequenceRenderArguments &/*args*/) {}

    friend class EncodePlanesLocalData_RAII;
    ///Used to allocate/free userdata passed to beginEncodePlanes,endEncodePlanes and encodePlane
    virtual void* allocateEncodePlanesUserData() { return (void*)0; }
    virtual void destroyEncodePlanesUserData(void* /*data*/) {}
    
    /**
     * @brief When writing multiple planes, should allocate data that are shared amongst all planes
     **/
    virtual void beginEncodeParts(void* user_data,
                                   const std::string& filename,
                                   OfxTime time,
                                   float pixelAspectRatio,
                                   LayerViewsPartsEnum partsSplitting,
                                   const std::map<int,std::string>& viewsToRender,
                                   const std::list<std::string>& planes,
                                   const OfxRectI& bounds);
    
    virtual void endEncodeParts(void* /*user_data*/) {}
    
    virtual void encodePart(void* user_data, const std::string& filename, const float *pixelData, int planeIndex, int rowBytes);
    
    /**
     * @brief Should return the view index needed to render. 
     * Possible return values:
     * -2: Indicates that we want to render what the host request via the render action (the default)
     * -1: Indicates that we want to render all views in a single file
     * >= 0: Indicates the view index to render
     **/
    virtual int getViewToRender() const { return -2; }
    
    virtual LayerViewsPartsEnum getPartsSplittingPreference() const { return eLayerViewsSinglePart; }
    
    /**
     * @brief Overload to return false if the given file extension is a video file extension or
     * true if this is an image file extension.
     **/
    virtual bool isImageFile(const std::string& fileExtension) const = 0;
    
    virtual void setOutputFrameRate(double /*fps*/) {}
    
    /**
     * @brief Must return whether your plug-in expects an input stream to be premultiplied or unpremultiplied to encode
     * properly into the file.
     **/
    virtual OFX::PreMultiplicationEnum getExpectedInputPremultiplication() const = 0;
    
    /**
     * @brief To implement if you added supportsDisplayWindow = true to GenericWriterDescribe(). 
     * Basically only EXR file format can handle this.
     **/
    virtual bool displayWindowSupportedByFormat(const std::string& /*filename*/) const  { return false; }

    
    OFX::Clip* _inputClip; //< Mantated input clip
    OFX::Clip *_outputClip; //< Mandated output clip
    OFX::StringParam  *_fileParam; //< The output file
    OFX::ChoiceParam *_frameRange; //<The frame range type
    OFX::IntParam* _firstFrame; //< the first frame if the frame range type is "Manual"
    OFX::IntParam* _lastFrame; //< the last frame if the frame range type is "Manual"
    OFX::ChoiceParam* _outputFormatType; //< the type of output format
    OFX::ChoiceParam* _outputFormat; //< the output format to render
    OFX::ChoiceParam* _premult;
    OFX::BooleanParam* _clipToProject;
    std::auto_ptr<GenericOCIO> _ocio;

private:
    
    
    class InputImagesHolder
    {
        std::list<const OFX::Image*> _imgs;
        std::list<OFX::ImageMemory*> _mems;
    public:
        
        InputImagesHolder();
        void addImage(const OFX::Image* img);
        void addMemory(OFX::ImageMemory* mem);
        ~InputImagesHolder();
    };
    
    /*
     * @brief Fetch the given plane for the given view at the given time and convert into suited color-space
     * using OCIO if needed. 
     *
     * If view == renderRequestedView the output image will be fetched on the output clip
     * and written to aswell.
     *
     * Post-condition:
     * - srcImgsHolder had the srcImg appended to it so it gets correctly released when it is
     * destroyed.
     * - tmpMemPtr is never NULL and points to either srcImg buffer or tmpMem buffer
     * - If a color-space conversion occured, tmpMem/tmpMemPtr is non-null and tmpMem was added to srcImgsHolder
     * so it gets correctly released upon destruction.
     *
     * This function MAY throw exceptions aborting the action, that is why we use the InputImagesHolder RAII style class
     * that will properly release resources.
     */
    void fetchPlaneConvertAndCopy(const std::string& plane,
                              int view,
                              int renderRequestedView,
                              double time,
                              const OfxRectI& renderWindow,
                              const OfxPointD& renderScale,
                              OFX::FieldEnum fieldToRender,
                              OFX::PreMultiplicationEnum pluginExpectedPremult,
                              OFX::PreMultiplicationEnum userPremult,
                              bool isOCIOIdentity,
                              InputImagesHolder* srcImgsHolder,
                              OfxRectI* bounds,
                              OFX::ImageMemory** tmpMem,
                              const OFX::Image** inputImage,
                              float** tmpMemPtr,
                              int* rowBytes,
                              OFX::PixelComponentEnum* mappedComponents);
    
    /**
     * @brief Retrieves the output filename at the given time and checks if the extension is supported.
     **/
    void getOutputFileNameAndExtension(OfxTime time,std::string& filename);
    
    /**
     * @brief Override if you want to do something when the output image/video file changed.
     * You shouldn't do any strong processing as this is called on the main thread and
     * the getRegionOfDefinition() and  decode() should open the file in a separate thread.
     **/
    virtual void onOutputFileChanged(const std::string& newFile, bool setColorSpace) = 0;

    /**
     * @brief Override to clear any cache you may have.
     **/
    virtual void clearAnyCache() {}
    
    void getOutputFormat(OfxTime time,OfxRectD& rod);

    void copyPixelData(const OfxRectI &renderWindow,
                       const OFX::Image* srcImg,
                       OFX::Image* dstImg)
    {
        const void* srcPixelData;
        OfxRectI srcBounds;
        OFX::PixelComponentEnum srcPixelComponents;
        OFX::BitDepthEnum srcBitDepth;
        int srcRowBytes;
        getImageData(srcImg, &srcPixelData, &srcBounds, &srcPixelComponents, &srcBitDepth, &srcRowBytes);
        int srcPixelComponentCount = srcImg->getPixelComponentCount();
        void* dstPixelData;
        OfxRectI dstBounds;
        OFX::PixelComponentEnum dstPixelComponents;
        OFX::BitDepthEnum dstBitDepth;
        int dstRowBytes;
        getImageData(dstImg, &dstPixelData, &dstBounds, &dstPixelComponents, &dstBitDepth, &dstRowBytes);
        int dstPixelComponentCount = dstImg->getPixelComponentCount();
        copyPixels(*this,
                   renderWindow,
                   srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                   dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(const OfxRectI &renderWindow,
                       const void *srcPixelData,
                       const OfxRectI& srcBounds,
                       OFX::PixelComponentEnum srcPixelComponents,
                       int srcPixelComponentCount,
                       OFX::BitDepthEnum srcBitDepth,
                       int srcRowBytes,
                       OFX::Image* dstImg)
    {
        void* dstPixelData;
        OfxRectI dstBounds;
        OFX::PixelComponentEnum dstPixelComponents;
        OFX::BitDepthEnum dstBitDepth;
        int dstRowBytes;
        getImageData(dstImg, &dstPixelData, &dstBounds, &dstPixelComponents, &dstBitDepth, &dstRowBytes);
        int dstPixelComponentCount = dstImg->getPixelComponentCount();
        copyPixels(*this,
                   renderWindow,
                   srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                   dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    }

    void copyPixelData(const OfxRectI &renderWindow,
                       const OFX::Image* srcImg,
                       void *dstPixelData,
                       const OfxRectI& dstBounds,
                       OFX::PixelComponentEnum dstPixelComponents,
                       int dstPixelComponentCount,
                       OFX::BitDepthEnum dstBitDepth,
                       int dstRowBytes)
    {
        const void* srcPixelData;
        OfxRectI srcBounds;
        OFX::PixelComponentEnum srcPixelComponents;
        OFX::BitDepthEnum srcBitDepth;
        int srcRowBytes;
        getImageData(srcImg, &srcPixelData, &srcBounds, &srcPixelComponents, &srcBitDepth, &srcRowBytes);
        int srcPixelComponentCount = srcImg->getPixelComponentCount();
        copyPixels(*this,
                   renderWindow,
                   srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                   dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    }
    
    void interleavePixelBuffers(const OfxRectI& renderWindow,
                                const void *srcPixelData,
                                const OfxRectI& bounds,
                                OFX::PixelComponentEnum srcPixelComponents,
                                int srcPixelComponentCount,
                                OFX::BitDepthEnum bitDepth,
                                int srcRowBytes,
                                const OfxRectI& dstBounds,
                                int dstPixelComponentStartIndex,
                                int dstPixelComponentCount,
                                int dstRowBytes,
                                void* dstPixelData);

    void unPremultPixelData(const OfxRectI &renderWindow,
                            const void *srcPixelData,
                            const OfxRectI& srcBounds,
                            OFX::PixelComponentEnum srcPixelComponents,
                            int srcPixelComponentCount,
                            OFX::BitDepthEnum srcPixelDepth,
                            int srcRowBytes,
                            void *dstPixelData,
                            const OfxRectI& dstBounds,
                            OFX::PixelComponentEnum dstPixelComponents,
                            int dstPixelComponentCount,
                            OFX::BitDepthEnum dstBitDepth,
                            int dstRowBytes);
    
    void premultPixelData(const OfxRectI &renderWindow,
                          const void *srcPixelData,
                          const OfxRectI& srcBounds,
                          OFX::PixelComponentEnum srcPixelComponents,
                          int srcPixelComponentCount,
                          OFX::BitDepthEnum srcPixelDepth,
                          int srcRowBytes,
                          void *dstPixelData,
                          const OfxRectI& dstBounds,
                          OFX::PixelComponentEnum dstPixelComponents,
                          int dstPixelComponentCount,
                          OFX::BitDepthEnum dstBitDepth,
                          int dstRowBytes);
};

class EncodePlanesLocalData_RAII
{
    GenericWriterPlugin* _w;
    void* data;
public:
    
    EncodePlanesLocalData_RAII(GenericWriterPlugin* w)
    : _w(w)
    , data(0)
    {
        data = w->allocateEncodePlanesUserData();
    }
    
    ~EncodePlanesLocalData_RAII()
    {
        _w->destroyEncodePlanesUserData(data);
    }
    
    void* getData() const  { return data; }
};

void GenericWriterDescribe(OFX::ImageEffectDescriptor &desc,OFX::RenderSafetyEnum safety, bool isMultiPlanar, bool isMultiView);
OFX::PageParamDescriptor* GenericWriterDescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, bool isVideoStreamPlugin, bool supportsRGBA, bool supportsRGB, bool supportsAlpha, const char* inputSpaceNameDefault, const char* outputSpaceNameDefault, bool supportsDisplayWindow);
void GenericWriterDescribeInContextEnd(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context,OFX::PageParamDescriptor* defaultPage);

#define mDeclareWriterPluginFactory(CLASS, LOADFUNCDEF, UNLOADFUNCDEF,ISVIDEOSTREAM) \
  class CLASS : public OFX::PluginFactoryHelper<CLASS>                       \
  {                                                                     \
  public:                                                                \
    CLASS(const std::string& id, unsigned int verMaj, unsigned int verMin):OFX::PluginFactoryHelper<CLASS>(id, verMaj, verMin){} \
    virtual void load() LOADFUNCDEF ;                                   \
    virtual void unload() UNLOADFUNCDEF ;                               \
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context); \
    bool isVideoStreamPlugin() const { return ISVIDEOSTREAM; }  \
    virtual void describe(OFX::ImageEffectDescriptor &desc);      \
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context); \
  };

#endif

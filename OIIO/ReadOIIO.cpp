/*
 OFX oiioReader plugin.
 Reads an image using the OpenImageIO library.
 
 Copyright (C) 2013 INRIA
 Author Alexandre Gauthier-Foichat alexandre.gauthier-foichat@inria.fr
 
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:
 
 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.
 
 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France
 
 */


#include "ReadOIIO.h"
#include "GenericOCIO.h"

#include <iostream>
#include <sstream>

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagecache.h>

OIIO_NAMESPACE_USING

#define kMetadataButton "show metadata"

////global OIIO image cache
static ImageCache* cache = 0;
static const bool kSupportsTiles = true;

ReadOIIOPlugin::ReadOIIOPlugin(OfxImageEffectHandle handle)
: GenericReaderPlugin(handle)
{
    
}

ReadOIIOPlugin::~ReadOIIOPlugin() {
    
}

void ReadOIIOPlugin::clearAnyCache() {
    ///flush the OIIO cache
    cache->invalidate_all();
}

void ReadOIIOPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) {
    if (paramName == kMetadataButton) {
        std::string filename;
        getCurrentFileName(filename);
        sendMessage(OFX::Message::eMessageMessage, "", metadata(filename));
    } else {
        GenericReaderPlugin::changedParam(args,paramName);
    }
}

void ReadOIIOPlugin::onInputFileChanged(const std::string &filename) {
    ///uncomment to use OCIO meta-data as a hint to set the correct color-space for the file.
    
#ifdef OFX_IO_USING_OCIO
    ImageSpec spec;

    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    if(!cache->get_imagespec(ustring(filename), spec)){
        setPersistentMessage(OFX::Message::eMessageError, "", cache->geterror());
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    ///find-out the image color-space
    ParamValue* colorSpaceValue = spec.find_attribute("oiio:ColorSpace",TypeDesc::STRING);

    //we found a color-space hint, use it to do the color-space conversion
    const char* colorSpaceStr;
    if (colorSpaceValue) {
        colorSpaceStr = *(const char**)colorSpaceValue->data();
        if (!strcmp(colorSpaceStr, "GammaCorrected")) {
            float gamma = spec.get_float_attribute("oiio:Gamma");
            if (gamma == 1.8) {
                if (_ocio->hasColorspace("Gamma1.8")) {
                    // nuke-default
                    _ocio->setInputColorspace("Gamma1.8");
                }
            } else if (gamma == 2.2) {
                if (_ocio->hasColorspace("Gamma2.2")) {
                    // nuke-default
                    _ocio->setInputColorspace("Gamma2.2");
                } else if (_ocio->hasColorspace("vd16")) {
                    // vd16 in spi-anim and spi-vfx
                    _ocio->setInputColorspace("vd16");
                }
            }
        } else if(!strcmp(colorSpaceStr, "sRGB")) {
            if (_ocio->hasColorspace("sRGB")) {
                // nuke-default
                _ocio->setInputColorspace("sRGB");
            } else if (_ocio->hasColorspace("rrt_srgb")) {
                // rrt_srgb in aces
                _ocio->setInputColorspace("rrt_srgb");
            } else if (_ocio->hasColorspace("srgb8")) {
                // srgb8 in spi-vfx
                _ocio->setInputColorspace("srgb8");
            }
        } else if(!strcmp(colorSpaceStr, "AdobeRGB")) {
            // ???
        } else if(!strcmp(colorSpaceStr, "Rec709")) {
            if (_ocio->hasColorspace("Rec709")) {
                // nuke-default
                _ocio->setInputColorspace("Rec709");
            } else if (_ocio->hasColorspace("rrt_rec709")) {
                // rrt_rec709 in aces
                _ocio->setInputColorspace("rrt_rec709");
            } else if (_ocio->hasColorspace("hd10")) {
                // hd10 in spi-anim and spi-vfx
                _ocio->setInputColorspace("hd10");
            }
        } else if(!strcmp(colorSpaceStr, "KodakLog")) {
            if (_ocio->hasColorspace("lg10")) {
                // lg10 in spi-vfx
                _ocio->setInputColorspace("lg10");
            }
        } else if(!strcmp(colorSpaceStr, "Linear")) {
            _ocio->setInputColorspace("scene_linear");
            // lnf in spi-vfx
        } else {
            //unknown color-space or Linear, don't do anything
        }
    }
#endif
}

void ReadOIIOPlugin::decode(const std::string& filename, OfxTime /*time*/, const OfxRectI& renderWindow, OFX::Image* dstImg)
{
    ImageSpec spec;
    
    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    if(!cache->get_imagespec(ustring(filename), spec)){
        setPersistentMessage(OFX::Message::eMessageError, "", cache->geterror());
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::PixelComponentEnum pixelComponents  = dstImg->getPixelComponents();

    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OIIO: can only read RGBA, RGB or Alpha components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    assert(kSupportsTiles || (renderWindow.x1 == 0 && renderWindow.x2 == spec.width && renderWindow.y1 == 0 && renderWindow.y2 == spec.height));

    int numChannels;
    int outputChannelBegin = 0;
    int chbegin;
    int chend;
    bool fillRGB = false;
    bool fillAlpha = false;
    switch(pixelComponents)
    {
        case OFX::ePixelComponentRGBA:
            numChannels = 4;
            fillRGB = (spec.nchannels == 1) || (spec.nchannels == 2);
            if (spec.nchannels == 1) {
                chbegin = chend = spec.alpha_channel;
                fillAlpha = (spec.alpha_channel == -1);
                outputChannelBegin = 3;
            } else {
                chbegin = 0;
                chend = std::min(spec.nchannels, numChannels);
                fillAlpha = (spec.alpha_channel == -1);
            }
            break;
        case OFX::ePixelComponentRGB:
            numChannels = 3;
            fillRGB = (spec.nchannels == 1) || (spec.nchannels == 2);
            if (spec.nchannels == 1) {
                chbegin = chend = -1;
            } else {
                chbegin = 0;
                chend = std::min(spec.nchannels, numChannels);
            }
            break;
        case OFX::ePixelComponentAlpha:
            numChannels = 1;
            chbegin = chend = spec.alpha_channel;
            fillAlpha = (spec.alpha_channel == -1);
            break;
        default:
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    if (fillRGB) {
        // fill RGB values with black
        assert(pixelComponents != OFX::ePixelComponentAlpha);
        char* lineStart = (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
        for (int y = renderWindow.y1; y < renderWindow.y2; ++y, lineStart += dstImg->getRowBytes()) {
            float *cur = (float*)lineStart;
            for (int x = renderWindow.x1; x < renderWindow.x2; ++x, cur += numChannels) {
                cur[0] = 0.;
                cur[1] = 0.;
                cur[2] = 0.;
            }
        }
    }
    if (fillAlpha) {
        // fill Alpha values with opaque
        assert(pixelComponents != OFX::ePixelComponentRGB);
        int outputChannelAlpha = (pixelComponents == OFX::ePixelComponentAlpha) ? 0 : 3;
        char* lineStart = (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
        for (int y = renderWindow.y1; y < renderWindow.y2; ++y, lineStart += dstImg->getRowBytes()) {
            float *cur = (float*)lineStart + outputChannelAlpha;
            for (int x = renderWindow.x1; x < renderWindow.x2; ++x, cur += numChannels) {
                cur[0] = 1.;
            }
        }
    }


    if(!cache->get_pixels(ustring(filename),
                          0, //subimage
                          0, //miplevel
                          renderWindow.x1, //x begin
                          renderWindow.x2, //x end
                          spec.height - renderWindow.y2, //y begin
                          spec.height - renderWindow.y1, //y end
                          0, //z begin
                          1, //z end
                          chbegin, //chan begin
                          chend, // chan end
                          TypeDesc::FLOAT, // data type
                          (float*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y2 - 1) + outputChannelBegin,// output buffer
                          numChannels * sizeof(float), //x stride
                          -dstImg->getRowBytes(), //y stride < make it invert Y
                          AutoStride //z stride
                          )) {
        
        setPersistentMessage(OFX::Message::eMessageError, "", cache->geterror());
        return;
    }
}

void ReadOIIOPlugin::getFrameRegionOfDefinition(const std::string& filename,OfxTime /*time*/,OfxRectD& rod) {
    ImageSpec spec;
    
    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    if(!cache->get_imagespec(ustring(filename), spec)){
        setPersistentMessage(OFX::Message::eMessageError, "", cache->geterror());
        return;
    }
    rod.x1 = spec.x;
    rod.x2 = spec.x + spec.width;
    rod.y1 = spec.y;
    rod.y2 = spec.y + spec.height;
}

std::string ReadOIIOPlugin::metadata(const std::string& filename)
{
    std::stringstream ss;

    ImageSpec spec;

    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    if(!cache->get_imagespec(ustring(filename), spec)){
        setPersistentMessage(OFX::Message::eMessageError, "", cache->geterror());
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    ss << filename << " : ";
    ss << "    channel list: ";
    for (int i = 0;  i < spec.nchannels;  ++i) {
        if (i < (int)spec.channelnames.size()) {
            ss << spec.channelnames[i];
        } else {
            ss << "unknown";
        }
        if (i < (int)spec.channelformats.size()) {
            ss << " (" << spec.channelformats[i].c_str() << ")";
        }
        if (i < spec.nchannels-1) {
            ss << ", ";
        }
    }
    ss << std::endl;

    if (spec.x || spec.y || spec.z) {
        ss << "    pixel data origin: x=" << spec.x << ", y=" << spec.y;
        if (spec.depth > 1) {
                ss << ", z=" << spec.z;
        }
        ss << std::endl;
    }
    if (spec.full_x || spec.full_y || spec.full_z ||
        (spec.full_width != spec.width && spec.full_width != 0) ||
        (spec.full_height != spec.height && spec.full_height != 0) ||
        (spec.full_depth != spec.depth && spec.full_depth != 0)) {
        ss << "    full/display size: " << spec.full_width << " x " << spec.full_height;
        if (spec.depth > 1) {
            ss << " x " << spec.full_depth;
        }
        ss << std::endl;
        ss << "    full/display origin: " << spec.full_x << ", " << spec.full_y;
        if (spec.depth > 1) {
            ss << ", " << spec.full_z;
        }
        ss << std::endl;
    }
    if (spec.tile_width) {
        ss << "    tile size: " << spec.tile_width << " x " << spec.tile_height;
        if (spec.depth > 1) {
            ss << " x " << spec.tile_depth;
        }
        ss << std::endl;
    }

    for (ImageIOParameterList::const_iterator p = spec.extra_attribs.begin(); p != spec.extra_attribs.end(); ++p) {
        std::string s = spec.metadata_val (*p, true);
        ss << "    " << p->name() << ": ";
        if (s == "1.#INF") {
            ss << "inf";
        } else {
            ss << s;
        }
        ss << std::endl;
    }

    return ss.str();
}

using namespace OFX;

void ReadOIIOPluginFactory::load() {
    if (!cache) {
        cache = ImageCache::create();
    }
}

void ReadOIIOPluginFactory::unload() {
    if (cache) {
        ImageCache::destroy(cache);
    }
}

#if 0
namespace OFX
{
    namespace Plugin
    {
        void getPluginIDs(OFX::PluginFactoryArray &ids)
        {
            static ReadOIIOPluginFactory p("fr.inria.openfx:ReadOIIO", 1, 0);
            ids.push_back(&p);
        }
    };
};
#endif

static std::string oiio_versions()
{
    std::ostringstream oss;
    int ver = openimageio_version();
    oss << "OIIO versions:" << std::endl;
    oss << "compiled with " << OIIO_VERSION_STRING << std::endl;
    oss << "running with " << ver/10000 << '.' << (ver%10000)/100 << '.' << (ver%100) << std::endl;
    return oss.str();
}

/** @brief The basic describe function, passed a plugin descriptor */
void ReadOIIOPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    GenericReaderDescribe(desc, kSupportsTiles);
    ///set OIIO to use as many threads as there are cores on the CPU
    if(!attribute("threads", 0)){
        std::cerr << "Failed to set the number of threads for OIIO" << std::endl;
    }
    
    // basic labels
    desc.setLabels("ReadOIIOOFX", "ReadOIIOOFX", "ReadOIIOOFX");
    desc.setPluginDescription("Read images using OpenImageIO.\n\n"
                              "OpenImageIO supports reading/writing the following file formats:\n"
                              "BMP (*.bmp)\n"
                              "Cineon (*.cin)\n"
                              "Direct Draw Surface (*.dds)\n"
                              "DPX (*.dpx)\n"
                              "Field3D (*.f3d)\n"
                              "FITS (*.fits)\n"
                              "HDR/RGBE (*.hdr)\n"
                              "Icon (*.ico)\n"
                              "IFF (*.iff)\n"
                              "JPEG (*.jpg *.jpe *.jpeg *.jif *.jfif *.jfi)\n"
                              "JPEG-2000 (*.jp2 *.j2k)\n"
                              "OpenEXR (*.exr)\n"
                              "Portable Network Graphics (*.png)\n"
#                           if OIIO_VERSION >= 10400
                              "PNM / Netpbm (*.pbm *.pgm *.ppm *.pfm)\n"
#                           else
                              "PNM / Netpbm (*.pbm *.pgm *.ppm)\n"
#                           endif
                              "PSD (*.psd *.pdd *.psb)\n"
                              "Ptex (*.ptex)\n"
                              "RLA (*.rla)\n"
                              "SGI (*.sgi *.rgb *.rgba *.bw *.int *.inta)\n"
                              "Softimage PIC (*.pic)\n"
                              "Targa (*.tga *.tpic)\n"
                              "TIFF (*.tif *.tiff *.tx *.env *.sm *.vsm)\n"
                              "Zfile (*.zfile)\n\n"
                              + oiio_versions());


#ifdef OFX_EXTENSIONS_TUTTLE

    const char* extensions[] = { "bmp", "cin", "dds", "dpx", "f3d", "fits", "hdr", "ico",
        "iff", "jpg", "jpe", "jpeg", "jif", "jfif", "jfi", "jp2", "j2k", "exr", "png",
        "pbm", "pgm", "ppm",
#     if OIIO_VERSION >= 10400
        "pfm",
#     endif
        "psd", "pdd", "psb", "ptex", "rla", "sgi", "rgb", "rgba", "bw", "int", "inta", "pic", "tga", "tpic", "tif", "tiff", "tx", "env", "sm", "vsm", "zfile", NULL };
    desc.addSupportedExtensions(extensions);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void ReadOIIOPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(), /*supportsRGBA =*/ false, /*supportsRGB =*/ false, /*supportsAlpha =*/ false, /*supportsTiles =*/ kSupportsTiles);

    OFX::PushButtonParamDescriptor* pb = desc.definePushButtonParam(kMetadataButton);
    pb->setLabels("Image info", "Image info", "Image info");
    pb->setHint("Shows information and metadata from the image at current time.");

    GenericReaderDescribeInContextEnd(desc, context, page, "reference", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* ReadOIIOPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum context)
{
    return new ReadOIIOPlugin(handle);
}

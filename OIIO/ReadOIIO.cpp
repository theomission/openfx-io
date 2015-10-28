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
 * OFX oiioReader plugin.
 * Reads an image using the OpenImageIO library.
 */

#include "ReadOIIO.h"

#include <iostream>
#include <set>
#include <sstream>
#include <fstream>
#include <cmath>
#include <cstddef>
#include <climits>
#include <algorithm>

#include "ofxsMacros.h"

GCC_DIAG_OFF(unused-parameter)
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagecache.h>
GCC_DIAG_ON(unused-parameter)

#include <ofxNatron.h>

#include "GenericOCIO.h"
#include "GenericReader.h"
#include "IOUtility.h"

OIIO_NAMESPACE_USING

#define OFX_READ_OIIO_USES_CACHE
#define OFX_READ_OIIO_NEWMENU

#define OFX_READ_OIIO_SUPPORTS_SUBIMAGES

#ifdef OFX_READ_OIIO_USES_CACHE
#define OFX_READ_OIIO_SHARED_CACHE
#endif

#define kPluginName "ReadOIIOOFX"
#define kPluginGrouping "Image/Readers"
#define kPluginDescription \
"Read images using OpenImageIO.\n\n" \
"Ouput is always Premultiplied (alpha is associated).\n\n" \
"The \"Image Premult\" parameter controls the file premultiplication state, " \
"and can be used to fix wrong file metadata (see the help for that parameter).\n"
#define kPluginIdentifier "fr.inria.openfx.ReadOIIO"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true
#ifdef OFX_READ_OIIO_USES_CACHE
#define kSupportsTiles true
#else
// It is more efficient to read full frames if no cache is used.
#define kSupportsTiles false
#endif
#ifdef OFX_READ_OIIO_NEWMENU
#define kIsMultiPlanar true
#else
#define kIsMultiPlanar false
#endif

#define kParamOutputLayer "outputLayer"
#define kParamOutputLayerLabel "Output Layer"
#define kParamOutputLayerHint "This is the layer from the file that will be mapped to the Color (RGBA) plane. In multi-planar hosts (such as Natron) " \
"other layers can be retrieved from a Shuffle node."

#define kParamShowMetadata "showMetadata"
#define kParamShowMetadataLabel "Image Info..."
#define kParamShowMetadataHint "Shows information and metadata from the image at current time."

// number of channels for hosts that don't support modifying choice menus (e.g. Nuke)
#define kDefaultLayerCount 16


#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
#define kParamUseDisplayWindowAsOrigin "originAtDisplayWindow"
#define kParamUseDisplayWindowAsOriginLabel "Use Display Window As Origin"
#define kParamUseDisplayWindowAsOriginHint "When checked, the bottom left corner (0,0) will shifted to the bottom left corner of the display window."
#endif



static bool gHostIsNatron   = false;


class ReadOIIOPlugin : public GenericReaderPlugin {

public:

    ReadOIIOPlugin(OfxImageEffectHandle handle);

    virtual ~ReadOIIOPlugin();

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;
    
    virtual void getClipComponents(const OFX::ClipComponentsArguments& args, OFX::ClipComponentsSetter& clipComponents) OVERRIDE FINAL;

    virtual void clearAnyCache() OVERRIDE FINAL;
private:

    
    virtual void onInputFileChanged(const std::string& filename, OFX::PreMultiplicationEnum *premult, OFX::PixelComponentEnum *components, int *componentCount) OVERRIDE FINAL;

    virtual bool isVideoStream(const std::string& /*filename*/) OVERRIDE FINAL { return false; }
    
    virtual void decode(const std::string& filename, OfxTime time, bool isPlayback, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds,
                             OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, int rowBytes) OVERRIDE FINAL
    {
        std::string rawComps;
        switch (pixelComponents) {
            case OFX::ePixelComponentAlpha:
                rawComps = kOfxImageComponentAlpha;
                break;
            case OFX::ePixelComponentRGB:
                rawComps = kOfxImageComponentRGB;
                break;
            case OFX::ePixelComponentRGBA:
                rawComps = kOfxImageComponentRGBA;
                break;
            default:
                OFX::throwSuiteStatusException(kOfxStatFailed);
                return;
        }
        decodePlane(filename, time, isPlayback, renderWindow, pixelData, bounds, pixelComponents, pixelComponentCount, rawComps, rowBytes);
    }
    
    virtual void decodePlane(const std::string& filename, OfxTime time, bool isPlayback, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds,
                             OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, const std::string& rawComponents, int rowBytes) OVERRIDE FINAL;
    
#ifndef OFX_READ_OIIO_NEWMENU
    void decodePlaneNoShuffleMenu(ImageInput* img, const ImageSpec& spec, const std::string& filename, OfxTime time, bool isPlayback, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds,OFX::PixelComponentEnum pixelComponents, int rowBytes);
#endif
    
    void openFile(const std::string& filename, bool useCache, ImageInput** img, ImageSpec* spec, int subimage);

    virtual bool getFrameBounds(const std::string& filename, OfxTime time, OfxRectI *bounds, double *par, std::string *error) OVERRIDE FINAL;

    virtual void onOutputComponentsParamChanged(OFX::PixelComponentEnum components) OVERRIDE FINAL;
    
    virtual void restoreState(const std::string& filename) OVERRIDE FINAL;
    
    std::string metadata(const std::string& filename);

    void updateSpec(const std::string &filename);

#ifdef OFX_READ_OIIO_NEWMENU
    void updateComponents(OFX::PixelComponentEnum outputComponents);

    void buildChannelMenus();

    void setDefaultChannels(OFX::PixelComponentEnum *components);
    
    void setChannels();

    void setDefaultChannelsFromRed(int rChannelIdx, bool mustSetChannelNames, OFX::PixelComponentEnum *components);
#endif

#ifdef OFX_READ_OIIO_USES_CACHE
    //// OIIO image cache
    ImageCache* _cache;
#endif
#ifdef OFX_READ_OIIO_NEWMENU
    OFX::ChoiceParam *_rChannel;
    OFX::ChoiceParam *_gChannel;
    OFX::ChoiceParam *_bChannel;
    OFX::ChoiceParam *_aChannel;
    OFX::StringParam *_rChannelName;
    OFX::StringParam *_gChannelName;
    OFX::StringParam *_bChannelName;
    OFX::StringParam *_aChannelName;
#else
    OFX::IntParam *_firstChannel;
#endif
#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
    OFX::BooleanParam* _useDisplayWindowAsOrigin;
#endif
    
    //Only accessed on the main-thread
    std::vector<ImageSpec> _subImagesSpec;
    bool _specValid; //!< does _spec contain anything valid?
    
    //We keep the name of the last file read when not in playback so that
    //if it changes we can invalidate the last file read from the OIIO cache since it is no longer useful.
    //The host cache will back it up on most case. The only useful case for the OIIO cache is when there are
    //multiple threads trying to read the same image.
    OFX::MultiThread::Mutex _lastFileReadNoPlaybackMutex;
    std::string _lastFileReadNoPlayback;
};

ReadOIIOPlugin::ReadOIIOPlugin(OfxImageEffectHandle handle)
: GenericReaderPlugin(handle, kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles,
#ifdef OFX_EXTENSIONS_NUKE
                      (OFX::getImageEffectHostDescription() && OFX::getImageEffectHostDescription()->isMultiPlanar) ? kIsMultiPlanar : false
#else
                      false
#endif
                      )
#ifdef OFX_READ_OIIO_USES_CACHE
#  ifdef OFX_READ_OIIO_SHARED_CACHE
, _cache(ImageCache::create(true)) // shared cache
#  else
, _cache(ImageCache::create(false)) // non-shared cache
#  endif
#endif
#ifdef OFX_READ_OIIO_NEWMENU
, _rChannel(0)
, _gChannel(0)
, _bChannel(0)
, _aChannel(0)
#else
, _firstChannel(0)
#endif
#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
, _useDisplayWindowAsOrigin(0)
#endif
, _subImagesSpec()
, _specValid(false)
, _lastFileReadNoPlaybackMutex()
, _lastFileReadNoPlayback()
{
#ifdef OFX_READ_OIIO_USES_CACHE
    // Always keep unassociated alpha.
    // Don't let OIIO premultiply, because if the image is 8bits,
    // it multiplies in 8bits (see TIFFInput::unassalpha_to_assocalpha()),
    // which causes a lot of precision loss.
    // see also https://github.com/OpenImageIO/oiio/issues/960
    _cache->attribute("unassociatedalpha", 1);
#endif
#ifdef OFX_READ_OIIO_NEWMENU
    _rChannel = fetchChoiceParam(kParamRChannel);
    _gChannel = fetchChoiceParam(kParamGChannel);
    _bChannel = fetchChoiceParam(kParamBChannel);
    _aChannel = fetchChoiceParam(kParamAChannel);
    _rChannelName = fetchStringParam(kParamRChannelName);
    _gChannelName = fetchStringParam(kParamGChannelName);
    _bChannelName = fetchStringParam(kParamBChannelName);
    _aChannelName = fetchStringParam(kParamAChannelName);
    
#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
    _useDisplayWindowAsOrigin = fetchBooleanParam(kParamUseDisplayWindowAsOrigin);
    assert(_useDisplayWindowAsOrigin);
#endif
    assert(_outputComponents && _rChannel && _gChannel && _bChannel && _aChannel &&
           _rChannelName && _bChannelName && _gChannelName && _aChannelName);
#else
    _firstChannel = fetchIntParam(kParamFirstChannel);
    assert(_outputComponents && _firstChannel);
#endif

#ifdef OFX_READ_OIIO_NEWMENU
    updateComponents(getOutputComponents());
#endif

    //Don't try to restore any state in here, do so in restoreState instead which is called
    //right away after the constructor.
    
    
    ///Set OIIO to use as many threads as there are cores
    if (!attribute("threads", 0)) {
#     ifdef DEBUG
        std::cerr << "Failed to set the number of threads for OIIO" << std::endl;
#     endif
    }

}

ReadOIIOPlugin::~ReadOIIOPlugin()
{
#ifdef OFX_READ_OIIO_USES_CACHE
#  ifdef OFX_READ_OIIO_SHARED_CACHE
    ImageCache::destroy(_cache); // don't teardown if it's a shared cache
#  else
    ImageCache::destroy(_cache, true); // teardown non-shared cache
#  endif
#endif
}

void ReadOIIOPlugin::clearAnyCache()
{
#ifdef OFX_READ_OIIO_USES_CACHE
    ///flush the OIIO cache
    _cache->invalidate_all(true);
#endif
}

#ifdef OFX_READ_OIIO_NEWMENU
void ReadOIIOPlugin::updateComponents(OFX::PixelComponentEnum outputComponents)
{
    switch (outputComponents) {
        case OFX::ePixelComponentRGBA: {
            _rChannel->setIsSecret(false);
            _bChannel->setIsSecret(false);
            _gChannel->setIsSecret(false);
            _aChannel->setIsSecret(false);
        }   break;
        case OFX::ePixelComponentRGB: {
            _rChannel->setIsSecret(false);
            _bChannel->setIsSecret(false);
            _gChannel->setIsSecret(false);
            _aChannel->setIsSecret(true);
        }   break;
        case OFX::ePixelComponentAlpha: {
            _rChannel->setIsSecret(true);
            _bChannel->setIsSecret(true);
            _gChannel->setIsSecret(true);
            _aChannel->setIsSecret(false);
        }   break;
        default:
            // unsupported components
            assert(false);
            break;
    }
}
#endif

void ReadOIIOPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamShowMetadata) {
        std::string filename;
        OfxStatus st = getFilenameAtTime(args.time, &filename);
        std::stringstream ss;
        if (st == kOfxStatOK) {
            ss << metadata(filename);
        } else {
            ss << "Impossible to read image info:\nCould not get filename at time " << args.time << '.';
        }
        sendMessage(OFX::Message::eMessageMessage, "", ss.str());
#ifdef OFX_READ_OIIO_NEWMENU
    } else if (paramName == kParamRChannel && args.reason == OFX::eChangeUserEdit) {
        int rChannelIdx;
        _rChannel->getValue(rChannelIdx);
        if (rChannelIdx >= kXChannelFirst) {
            setDefaultChannelsFromRed(rChannelIdx - kXChannelFirst, true, NULL);
        }
        
        std::string optionName;
        _rChannel->getOption(rChannelIdx, optionName);
        _rChannelName->setValue(optionName);
    } else if (paramName == kParamGChannel && args.reason == OFX::eChangeUserEdit) {
        int gChannelIdx;
        _gChannel->getValue(gChannelIdx);
        std::string optionName;
        _gChannel->getOption(gChannelIdx, optionName);
        _gChannelName->setValue(optionName);
    } else if (paramName == kParamBChannel && args.reason == OFX::eChangeUserEdit) {
        int bChannelIdx;
        _bChannel->getValue(bChannelIdx);
        std::string optionName;
        _bChannel->getOption(bChannelIdx, optionName);
        _bChannelName->setValue(optionName);
    }
    else if (paramName == kParamAChannel && args.reason == OFX::eChangeUserEdit) {
        int aChannelIdx;
        _aChannel->getValue(aChannelIdx);
        std::string optionName;
        _aChannel->getOption(aChannelIdx, optionName);
        _aChannelName->setValue(optionName);
#endif
    } else {
        GenericReaderPlugin::changedParam(args,paramName);
    }
}

void
ReadOIIOPlugin::onOutputComponentsParamChanged(OFX::PixelComponentEnum components)
{
#ifdef OFX_READ_OIIO_NEWMENU
    updateComponents(components);
#else
    // set the first channel to the alpha channel if output is alpha
    if (components == OFX::ePixelComponentAlpha) {
        std::string filename;
        _fileParam->getValue(filename);
        OFX::PreMultiplicationEnum premult;
        OFX::PixelComponentEnum components;
        int compsCount;
        onInputFileChanged(filename, &premult, &components, &compsCount);
    }
#endif
}

static std::string makeNatronCustomChannel(const std::string& layer,const std::vector<std::string>& channels)
{
    std::string ret(kNatronOfxImageComponentsPlane);
    ret.append(layer);
    for (std::size_t i = 0; i < channels.size(); ++i) {
        ret.append(kNatronOfxImageComponentsPlaneChannel);
        ret.append(channels[i]);
    }
    return ret;
    
}

void
ReadOIIOPlugin::getClipComponents(const OFX::ClipComponentsArguments& args, OFX::ClipComponentsSetter& clipComponents)
{
    //Should only be called if multi-planar
    assert(isMultiPlanar());
    
    clipComponents.addClipComponents(*_outputClip, getOutputComponents());
    clipComponents.setPassThroughClip(NULL, args.time, args.view);
    
    if (_specValid) {
        
        std::map<std::string,std::vector<std::string> > layers;
        for (std::size_t s = 0; s < _subImagesSpec.size(); ++s) {
            for (int i = 0; i < _subImagesSpec[s].nchannels; ++i) {
                if (i < (int)_subImagesSpec[s].channelnames.size()) {
                    const std::string& chan = _subImagesSpec[s].channelnames[i];
                    std::size_t foundLastDot = chan.find_last_of('.');
                    
                    //Consider everything before the last dot as being a layer
                    if (foundLastDot != std::string::npos) {
                        std::string layer = chan.substr(0,foundLastDot);
                        std::string channel = chan.substr(foundLastDot + 1,std::string::npos);
                        
                        std::map<std::string,std::vector<std::string> >::iterator foundLayer = layers.find(layer);
                        if (foundLayer == layers.end()) {
                            ///Add a new vector of channels for the layer
                            std::vector<std::string> chanVec;
                            chanVec.push_back(channel);
                            layers.insert(std::make_pair(layer, chanVec));
                            
                        } else {
                            ///Complete the vector
                            foundLayer->second.push_back(channel);
                        }
                        
                    } else {
                        //The channel does not have a layer prefix, it is either R,G,B,A or a custom single channel component
                        //If RGBA, don't consider it as it is already considered with the output components.
                        if (chan != "R" && chan != "r" && chan != "red" &&
                            chan != "G" && chan != "g" && chan != "green" &&
                            chan != "B" && chan != "b" && chan != "blue" &&
                            chan != "A" && chan != "a" && chan != "alpha") {
                            
                            std::vector<std::string> chanVec;
                            chanVec.push_back(chan);
                            layers.insert(std::make_pair(chan, chanVec));
                        }
                    }
                }
            }
        }
        for (std::map<std::string,std::vector<std::string> >::iterator it = layers.begin(); it!=layers.end(); ++it) {
            std::string component = makeNatronCustomChannel(it->first, it->second);
            clipComponents.addClipComponents(*_outputClip, component);
        }
    }
}

static bool has_suffix(const std::string &str, const std::string &suffix)
{
    return (str.size() >= suffix.size() &&
            str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0);
}

#ifdef OFX_READ_OIIO_NEWMENU
void
ReadOIIOPlugin::buildChannelMenus()
{
    if (gHostIsNatron) {
        // the choice menu can only be modified in Natron
        // Natron supports changing the entries in a choiceparam
        // Nuke (at least up to 8.0v3) does not
        _rChannel->resetOptions();
        _gChannel->resetOptions();
        _bChannel->resetOptions();
        _aChannel->resetOptions();
        _rChannel->appendOption("0");
        _rChannel->appendOption("1");
        _gChannel->appendOption("0");
        _gChannel->appendOption("1");
        _bChannel->appendOption("0");
        _bChannel->appendOption("1");
        _aChannel->appendOption("0");
        _aChannel->appendOption("1");
        assert(_rChannel->getNOptions() == kXChannelFirst);
        assert(_gChannel->getNOptions() == kXChannelFirst);
        assert(_bChannel->getNOptions() == kXChannelFirst);
        assert(_aChannel->getNOptions() == kXChannelFirst);
        if (_specValid) {
            for (std::size_t s = 0; s < _subImagesSpec.size(); ++s) {
                for (int i = 0; i < _subImagesSpec[s].nchannels; ++i) {
                    if (i < (int)_subImagesSpec[s].channelnames.size()) {
                        _rChannel->appendOption(_subImagesSpec[s].channelnames[i]);
                        _bChannel->appendOption(_subImagesSpec[s].channelnames[i]);
                        _gChannel->appendOption(_subImagesSpec[s].channelnames[i]);
                        _aChannel->appendOption(_subImagesSpec[s].channelnames[i]);
                    } else {
                        std::ostringstream oss;
                        oss << "channel " << i;
                        _rChannel->appendOption(oss.str());
                        _gChannel->appendOption(oss.str());
                        _bChannel->appendOption(oss.str());
                        _aChannel->appendOption(oss.str());
                    }
                }
            }
        }
    }
}

// called when the red channel is set
// from the red channel name, infer the corresponding G,B,A channel values
void
ReadOIIOPlugin::setDefaultChannelsFromRed(int rChannelIdx, bool mustSetChannelNames, OFX::PixelComponentEnum *components)
{
    assert(rChannelIdx >= 0);
    if (!_specValid) {
        return;
    }
    int channelsVisitedSoFar = 0;
    std::string rFullName;
    int specIndex = -1;
    for (std::size_t s = 0; s < _subImagesSpec.size(); ++s) {
        if (rChannelIdx < channelsVisitedSoFar + _subImagesSpec[s].nchannels && rChannelIdx >= channelsVisitedSoFar) {
            rFullName = _subImagesSpec[s].channelnames[rChannelIdx - channelsVisitedSoFar];
            specIndex = (int)s;
            break;
        }
        channelsVisitedSoFar += _subImagesSpec[s].nchannels;
    }
    if (rFullName.empty()) {
        // no name, can't do anything
        return;
    }
    assert(specIndex != -1);
    std::string rChannelName;
    std::string layerDotViewDot;
    // the EXR channel naming convention is layer.view.channel
    // ref: http://www.openexr.com/MultiViewOpenEXR.pdf

    // separate layer.view. from channel
    size_t lastdot = rFullName.find_last_of(".");
    if (lastdot == std::string::npos) {
        // no dot, channel name is the full name
        rChannelName = rFullName;
    } else {
        layerDotViewDot = rFullName.substr(0, lastdot + 1);
        rChannelName = rFullName.substr(lastdot + 1);
    }
    // now check if the channel name looks like red (normally, it should be "R")
    if (rChannelName != "R" &&
        rChannelName != "r" &&
        rChannelName != "red") {
        // not red, can't do anything
        return;
    }
    std::string gFullName;
    std::string bFullName;
    std::string aFullName;

    if (rChannelName == "R") {
        gFullName = layerDotViewDot + "G";
        bFullName = layerDotViewDot + "B";
        aFullName = layerDotViewDot + "A";
    } else if (rChannelName == "r") {
        gFullName = layerDotViewDot + "g";
        bFullName = layerDotViewDot + "b";
        aFullName = layerDotViewDot + "a";
    } else if (rChannelName == "red") {
        gFullName = layerDotViewDot + "green";
        bFullName = layerDotViewDot + "blue";
        aFullName = layerDotViewDot + "alpha";
    }
    bool gSet = false;
    bool bSet = false;
    bool aSet = false;
    int layerViewChannels = 0;
    for (size_t i = 0; i < _subImagesSpec[specIndex].channelnames.size(); ++i) {
        // check if the channel is within the layer/view, and count number of channels
        layerViewChannels = 0;
        size_t channelDot = _subImagesSpec[specIndex].channelnames[i].find_last_of(".");
        if ((lastdot == std::string::npos && channelDot == std::string::npos) ||
            (lastdot != std::string::npos && channelDot != std::string::npos)) {
            ++layerViewChannels;
        }
       
        if (rFullName == _subImagesSpec[specIndex].channelnames[i]) {
            int chanIndex = rChannelIdx + 1 + kXChannelFirst;
            for (size_t j = i+1; j < _subImagesSpec[specIndex].channelnames.size(); ++j, ++chanIndex) {
                if (_subImagesSpec[specIndex].channelnames[j] == gFullName) {
                    _gChannel->setValue(chanIndex);
                    if (mustSetChannelNames) {
                        std::string optionName;
                        _gChannel->getOption(chanIndex, optionName);
                        _gChannelName->setValue(optionName);
                    }
                    gSet = true;
                    continue;
                }
                if (_subImagesSpec[specIndex].channelnames[j] == bFullName) {
                    _bChannel->setValue(chanIndex);
                    if (mustSetChannelNames) {
                        std::string optionName;
                        _bChannel->getOption(chanIndex, optionName);
                        _bChannelName->setValue(optionName);
                    }
                    bSet = true;
                    continue;
                }
                if (_subImagesSpec[specIndex].channelnames[j] == aFullName) {
                    _aChannel->setValue(chanIndex);
                    if (mustSetChannelNames) {
                        std::string optionName;
                        _aChannel->getOption(chanIndex, optionName);
                        _aChannelName->setValue(optionName);
                    }
                    aSet = true;
                    continue;
                }
            }
            
        }
        break;
    }
    if (!gSet) {
        _gChannel->setValue(0);
        if (mustSetChannelNames) {
            std::string optionName;
            _gChannel->getOption(0, optionName);
            _gChannelName->setValue(optionName);
        }
    }
    if (!bSet) {
        _bChannel->setValue(0);
        if (mustSetChannelNames) {
            std::string optionName;
            _bChannel->getOption(0, optionName);
            _bChannelName->setValue(optionName);
        }
    }
    if (!aSet) {
        if (_subImagesSpec[specIndex].alpha_channel >= 0) {
            _aChannel->setValue(kXChannelFirst + _subImagesSpec[specIndex].alpha_channel);
            if (mustSetChannelNames) {
                std::string optionName;
                _aChannel->getOption(kXChannelFirst + _subImagesSpec[specIndex].alpha_channel, optionName);
                _aChannelName->setValue(optionName);
            }
        } else if (layerViewChannels != 4) {
            // Output is Opaque with alpha=0 by default,
            // but premultiplication is set to opaque.
            // That way, chaining with a Roto node works correctly.
            // Alpha is set to 0 and premult is set to Opaque.
            // That way, the Roto node can be conveniently used to draw a mask. This shouldn't
            // disturb anything else in the process, since Opaque premult means that alpha should
            // be considered as being 1 everywhere, whatever the actual alpha value is.
            // see GenericWriterPlugin::render, if (userPremult == OFX::eImageOpaque...
            if (components && *components == OFX::ePixelComponentRGBA) {
                *components = OFX::ePixelComponentRGB;
            }
            _aChannel->setValue(0);
            if (mustSetChannelNames) {
                std::string optionName;
                _aChannel->getOption(0, optionName);
                _aChannelName->setValue(optionName);
            }
        } else {
            // if there are exactly 4 channels in this layer/view, then the
            // remaining one should be Alpha
            for (size_t i = 0; i < _subImagesSpec[specIndex].channelnames.size(); ++i) {
                // check if the channel is within the layer/view
                if (lastdot == std::string::npos ||
                    _subImagesSpec[specIndex].channelnames[i].compare(0, layerDotViewDot.length(), layerDotViewDot) == 0) {
                    if (_subImagesSpec[specIndex].channelnames[i] != rFullName &&
                        _subImagesSpec[specIndex].channelnames[i] != gFullName &&
                        _subImagesSpec[specIndex].channelnames[i] != bFullName) {
                        _aChannel->setValue(kXChannelFirst + i);
                        if (mustSetChannelNames) {
                            std::string optionName;
                            _aChannel->getOption(kXChannelFirst + i, optionName);
                            _aChannelName->setValue(optionName);
                        }
                    }
                }
            }
        }
    }
}



// called after changing the filename, set all channels
void
ReadOIIOPlugin::setDefaultChannels(OFX::PixelComponentEnum *components)
{
    if (!_specValid) {
        return;
    }
    {
        int rChannelIdx = -1;

        // first, look for the main red channel
        for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
            if (_subImagesSpec[0].channelnames[i] == "R" ||
                _subImagesSpec[0].channelnames[i] == "r" ||
                _subImagesSpec[0].channelnames[i] == "red") {
                rChannelIdx = i;
                break; // found!
            }
        }

        if (rChannelIdx < 0) {
            // find a name which ends with ".R", ".r" or ".red"
            for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
                if (has_suffix(_subImagesSpec[0].channelnames[i], ".R") ||
                    has_suffix(_subImagesSpec[0].channelnames[i], ".r") ||
                    has_suffix(_subImagesSpec[0].channelnames[i], ".red")) {
                    rChannelIdx = i;
                    break; // found!
                }
            }
        }

        if (rChannelIdx >= 0) {
            // red was found
            _rChannel->setValue(kXChannelFirst + rChannelIdx);
            setDefaultChannelsFromRed(rChannelIdx, false, components);
            return;
        } else if (_subImagesSpec[0].nchannels >= 3) {
            _rChannel->setValue(kXChannelFirst + 0);
        } else if (_subImagesSpec[0].nchannels == 1) {
            _rChannel->setValue(kXChannelFirst);
        } else {
            _rChannel->setValue(0);
        }
    }
    // could not find red. look for green, blue, alpha.
    {
        int gChannelIdx = -1;

        // first, look for the main green channel
        for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
            if (_subImagesSpec[0].channelnames[i] == "G" ||
                _subImagesSpec[0].channelnames[i] == "g" ||
                _subImagesSpec[0].channelnames[i] == "green") {
                gChannelIdx = i;
                break; // found!
            }
        }

        if (gChannelIdx < 0) {
            // find a name which ends with ".G", ".g" or ".green"
            for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
                if (has_suffix(_subImagesSpec[0].channelnames[i], ".G") ||
                    has_suffix(_subImagesSpec[0].channelnames[i], ".g") ||
                    has_suffix(_subImagesSpec[0].channelnames[i], ".green")) {
                    gChannelIdx = i;
                    break; // found!
                }
            }
        }

        if (gChannelIdx >= 0) {
            // green was found
            _gChannel->setValue(kXChannelFirst + gChannelIdx);
        } else if (_subImagesSpec[0].nchannels >= 3) {
            _gChannel->setValue(kXChannelFirst + 1);
        } else if (_subImagesSpec[0].nchannels == 1) {
            _gChannel->setValue(kXChannelFirst);
        } else {
            _gChannel->setValue(0);
        }
    }
    {
        int bChannelIdx = -1;

        // first, look for the main blue channel
        for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
            if (_subImagesSpec[0].channelnames[i] == "B" ||
                _subImagesSpec[0].channelnames[i] == "b" ||
                _subImagesSpec[0].channelnames[i] == "blue") {
                bChannelIdx = i;
                break; // found!
            }
        }

        if (bChannelIdx < 0) {
            // find a name which ends with ".B", ".b" or ".blue"
            for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
                if (has_suffix(_subImagesSpec[0].channelnames[i], ".B") ||
                    has_suffix(_subImagesSpec[0].channelnames[i], ".b") ||
                    has_suffix(_subImagesSpec[0].channelnames[i], ".blue")) {
                    bChannelIdx = i;
                    break; // found!
                }
            }
        }

        if (bChannelIdx >= 0) {
            // blue was found
            _bChannel->setValue(kXChannelFirst + bChannelIdx);
        } else if (_subImagesSpec[0].nchannels >= 3) {
            _bChannel->setValue(kXChannelFirst + 2);
        } else if (_subImagesSpec[0].nchannels == 1) {
            _bChannel->setValue(kXChannelFirst);
        } else {
            _bChannel->setValue(0);
        }
    }
    {
        int aChannelIdx = -1;

        // first, look for the main alpha channel
        for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
            if (_subImagesSpec[0].channelnames[i] == "A" ||
                _subImagesSpec[0].channelnames[i] == "a" ||
                _subImagesSpec[0].channelnames[i] == "alpha") {
                aChannelIdx = i;
                break; // found!
            }
        }

        if (aChannelIdx < 0) {
            // find a name which ends with ".A", ".a" or ".alpha"
            for (std::size_t i = 0; i < _subImagesSpec[0].channelnames.size(); ++i) {
                if (has_suffix(_subImagesSpec[0].channelnames[i], ".A") ||
                    has_suffix(_subImagesSpec[0].channelnames[i], ".a") ||
                    has_suffix(_subImagesSpec[0].channelnames[i], ".alpha")) {
                    aChannelIdx = i;
                    break; // found!
                }
            }
        }

        if (aChannelIdx >= 0) {
            // alpha was found
            _aChannel->setValue(kXChannelFirst + aChannelIdx);
        } else if (_subImagesSpec[0].nchannels >= 4) {
            _aChannel->setValue(kXChannelFirst + 3);
        } else if (_subImagesSpec[0].nchannels == 1) {
            _aChannel->setValue(kXChannelFirst);
        } else {
            if (components && *components == OFX::ePixelComponentRGBA) {
                *components = OFX::ePixelComponentRGB; // so that premult is set to opaque
            }
            _aChannel->setValue(0);
        }
    }
}

void
ReadOIIOPlugin::setChannels()
{
    
#ifdef OFX_READ_OIIO_NEWMENU
    OFX::ChoiceParam* channelParams[4] = { _rChannel, _gChannel, _bChannel, _aChannel };
    OFX::StringParam* stringParams[4] = { _rChannelName, _gChannelName, _bChannelName, _aChannelName };

    for (int c = 0; c < 4; ++c) {
        
        std::string channelString;
        stringParams[c]->getValue(channelString);

        bool channelSet = false;

        if ( !channelString.empty() ) {
            // Restore the index from the serialized string
            for (int i = 0; i < channelParams[c]->getNOptions(); ++i) {
                std::string option;
                channelParams[c]->getOption(i, option);
                if (option == channelString) {
                    channelParams[c]->setValue(i);
                    channelSet = true;
                    break;
                }
            }
        }
        if (!channelSet) {
            // We are in the case where the strings were not serialized (or didn't exist in that project),
            // or the named channel doesnt exist,
            // so we blindly trust the values in the channels
            
            //Edit: don't do this if the channel menus are empty (i.e: containing only 2 entries (0 and 1)) otherwise
            //we will always set the strings to "0" when building new instances of the plug-in and when
            //inputFileChanged() will be called further on, it will set the channel index to 0 since the string won't be empty!
            int nChoices = channelParams[c]->getNOptions();
            if (nChoices > 2) {
                int idx;
                channelParams[c]->getValue(idx);
                std::string option;
                channelParams[c]->getOption(idx, option);
                assert(option != channelString); // if they were equal, it should have triggered channelSet = true above
                stringParams[c]->setValue(option);
            }
        }
    }
#else
    _firstChannel->setDisplayRange(0, _spec.nchannels);
    // set the first channel to the alpha channel if output is alpha
    if (_spec.alpha_channel != -1 && components == OFX::ePixelComponentAlpha) {
        _firstChannel->setValue(_spec.alpha_channel);
    }
#endif
}

#endif // OFX_READ_OIIO_NEWMENU

void
ReadOIIOPlugin::updateSpec(const std::string &filename)
{
    _specValid = false;
# ifdef OFX_READ_OIIO_USES_CACHE
    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    ImageSpec spec;
    _subImagesSpec.clear();
    int subImageIndex = 0;
#ifdef OFX_READ_OIIO_SUPPORTS_SUBIMAGES
    while (
#else
    if (
#endif
           _cache->get_imagespec(ustring(filename), spec, subImageIndex)) {
        _subImagesSpec.push_back(spec);
        ++subImageIndex;
    }
    if (_subImagesSpec.empty()) {
        return;
    }
# else
    std::auto_ptr<ImageInput> img(ImageInput::open(filename));
    if (!img.get()) {
        return;
    }
    int subImageIndex = 0;
    ImageSpec spec;
           int subImageIndex = 0;
#ifdef OFX_READ_OIIO_SUPPORTS_SUBIMAGES
           while (
#else
            if (
#endif
           img->seek_subimage(subImageIndex, 0, spec)) {
        _subImagesSpec.push_back(spec);
        ++subImageIndex;
    }
    if (_subImagesSpec.empty()) {
        return;
    }
# endif
    _specValid = true;
    
#ifdef OFX_READ_OIIO_USES_CACHE
    //Only support tiles if tile size is set
    setSupportsTiles(_subImagesSpec[0].tile_width != 0 && _subImagesSpec[0].tile_width != _subImagesSpec[0].full_width && _subImagesSpec[0].tile_height != 0 && _subImagesSpec[0].tile_height != _subImagesSpec[0].full_height);
#endif
}

void
ReadOIIOPlugin::restoreState(const std::string& filename)
{
    //Update OIIO spec
    updateSpec(filename);
    
#ifdef OFX_READ_OIIO_NEWMENU
    //Update RGBA parameters visibility according to the output components
    updateComponents(getOutputComponents());
    
    //Build available channels from OIIO spec
    buildChannelMenus();
    // set the default values for R, G, B, A channels
    setDefaultChannels(NULL);
    //Restore channels from the channel strings serialized
    setChannels();
    
    ///http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#SettingParams
    ///The Create instance action is in the list of actions where you can set param values
    
    
#else
    if (_specValid) {
        _firstChannel->setDisplayRange(0, _subImagesSpec[0].nchannels);
    }
    
#endif
    
}

void
ReadOIIOPlugin::onInputFileChanged(const std::string &filename,
                                   OFX::PreMultiplicationEnum *premult,
                                   OFX::PixelComponentEnum *components,
                                   int *componentCount)
{
    updateSpec(filename);
    if (!_specValid) {
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("ReadOIIO: cannot open file ") + filename);
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    }
# ifdef OFX_IO_USING_OCIO
    ///find-out the image color-space
    const ParamValue* colorSpaceValue = _subImagesSpec[0].find_attribute("oiio:ColorSpace", TypeDesc::STRING);
    const ParamValue* photoshopICCProfileValue = _subImagesSpec[0].find_attribute("photoshop:ICCProfile", TypeDesc::STRING);
    //photoshop:ICCProfile: "HDTV (Rec. 709)"

    //we found a color-space hint, use it to do the color-space conversion
    const char* colorSpaceStr = NULL;
    if (colorSpaceValue) {
        colorSpaceStr = *(const char**)colorSpaceValue->data();
    } else if (photoshopICCProfileValue) {
        const char* ICCProfileStr = *(const char**)photoshopICCProfileValue->data();
        if (!strcmp(ICCProfileStr, "HDTV (Rec. 709)") ||
            !strcmp(ICCProfileStr, "SDTV NTSC") ||
            !strcmp(ICCProfileStr, "SDTV PAL") ||
            !strcmp(ICCProfileStr, "HDTV (Rec. 709) 16-235") ||
            !strcmp(ICCProfileStr, "SDTV NTSC 16-235") ||
            !strcmp(ICCProfileStr, "SDTV PAL 16-235") ||
            !strcmp(ICCProfileStr, "SDTV NTSC 16-235")) {
            colorSpaceStr = "Rec709";
        } else if (!strcmp(ICCProfileStr, "sRGB IEC61966-2.1")) {
            colorSpaceStr = "sRGB";
        } else if (!strcmp(ICCProfileStr, "Universal Camera Film Printing Density)")) {
            colorSpaceStr = "KodakLog";
        }
    }
    if (!colorSpaceStr) {
        // no colorspace... we'll probably have to try something else, then.
        // we set the following defaults:
        // sRGB for 8-bit images
        // Rec709 for 10-bits, 12-bits or 16-bits integer images
        // Linear for anything else
        switch (_subImagesSpec[0].format.basetype) {
            case TypeDesc::UCHAR:
            case TypeDesc::CHAR:
                colorSpaceStr = "sRGB";
                break;
            case TypeDesc::USHORT:
            case TypeDesc::SHORT:
                if (has_suffix(filename, ".cin") || has_suffix(filename, ".dpx") ||
                    has_suffix(filename, ".CIN") || has_suffix(filename, ".DPX")) {
                    // Cineon or DPX file
                    colorSpaceStr = "KodakLog";
                } else {
                    colorSpaceStr = "Rec709";
                }
                break;
            default:
                colorSpaceStr = "Linear";
                break;
        }
    }
    if (colorSpaceStr) {
        if (!strcmp(colorSpaceStr, "GammaCorrected")) {
            float gamma = _subImagesSpec[0].get_float_attribute("oiio:Gamma");
            if (std::fabs(gamma-1.8) < 0.01) {
                if (_ocio->hasColorspace("Gamma1.8")) {
                    // nuke-default
                    _ocio->setInputColorspace("Gamma1.8");
                }
            } else if (std::fabs(gamma-2.2) < 0.01) {
                if (_ocio->hasColorspace("Gamma2.2")) {
                    // nuke-default
                    _ocio->setInputColorspace("Gamma2.2");
                } else if (_ocio->hasColorspace("VD16")) {
                    // VD16 in blender
                    _ocio->setInputColorspace("VD16");
                } else if (_ocio->hasColorspace("vd16")) {
                    // vd16 in spi-anim and spi-vfx
                    _ocio->setInputColorspace("vd16");
                } else if (_ocio->hasColorspace("sRGB")) {
                    // nuke-default and blender
                    _ocio->setInputColorspace("sRGB");
                } else if (_ocio->hasColorspace("sRGB (D60 sim.)")) {
                    // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
                    _ocio->setInputColorspace("sRGB (D60 sim.)");
                } else if (_ocio->hasColorspace("out_srgbd60sim")) {
                    // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
                    _ocio->setInputColorspace("out_srgbd60sim");
                } else if (_ocio->hasColorspace("rrt_Gamma2.2")) {
                    // rrt_Gamma2.2 in aces 0.7.1
                    _ocio->setInputColorspace("rrt_Gamma2.2");
                } else if (_ocio->hasColorspace("rrt_srgb")) {
                    // rrt_srgb in aces 0.1.1
                    _ocio->setInputColorspace("rrt_srgb");
                } else if (_ocio->hasColorspace("srgb8")) {
                    // srgb8 in spi-vfx
                    _ocio->setInputColorspace("srgb8");
                } else if (_ocio->hasColorspace("vd16")) {
                    // vd16 in spi-anim
                    _ocio->setInputColorspace("vd16");
                }
            }
        } else if(!strcmp(colorSpaceStr, "sRGB")) {
            if (_ocio->hasColorspace("sRGB")) {
                // nuke-default and blender
                _ocio->setInputColorspace("sRGB");
            } else if (_ocio->hasColorspace("sRGB (D60 sim.)")) {
                // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
                _ocio->setInputColorspace("sRGB (D60 sim.)");
            } else if (_ocio->hasColorspace("out_srgbd60sim")) {
                // out_srgbd60sim or "sRGB (D60 sim.)" in aces 1.0.0
                _ocio->setInputColorspace("out_srgbd60sim");
            } else if (_ocio->hasColorspace("rrt_Gamma2.2")) {
                // rrt_Gamma2.2 in aces 0.7.1
                _ocio->setInputColorspace("rrt_Gamma2.2");
            } else if (_ocio->hasColorspace("rrt_srgb")) {
                // rrt_srgb in aces 0.1.1
                _ocio->setInputColorspace("rrt_srgb");
            } else if (_ocio->hasColorspace("srgb8")) {
                // srgb8 in spi-vfx
                _ocio->setInputColorspace("srgb8");
            } else if (_ocio->hasColorspace("Gamma2.2")) {
                // nuke-default
                _ocio->setInputColorspace("Gamma2.2");
            } else if (_ocio->hasColorspace("srgb8")) {
                // srgb8 in spi-vfx
                _ocio->setInputColorspace("srgb8");
            } else if (_ocio->hasColorspace("vd16")) {
                // vd16 in spi-anim
                _ocio->setInputColorspace("vd16");
            }
        } else if(!strcmp(colorSpaceStr, "AdobeRGB")) {
            // ???
        } else if(!strcmp(colorSpaceStr, "Rec709")) {
            if (_ocio->hasColorspace("Rec709")) {
                // nuke-default
                _ocio->setInputColorspace("Rec709");
            } else if (_ocio->hasColorspace("nuke_rec709")) {
                // blender
                _ocio->setInputColorspace("nuke_rec709");
            } else if (_ocio->hasColorspace("Rec.709 - Full")) {
                // out_rec709full or "Rec.709 - Full" in aces 1.0.0
                _ocio->setInputColorspace("Rec.709 - Full");
            } else if (_ocio->hasColorspace("out_rec709full")) {
                // out_rec709full or "Rec.709 - Full" in aces 1.0.0
                _ocio->setInputColorspace("out_rec709full");
            } else if (_ocio->hasColorspace("rrt_rec709_full_100nits")) {
                // rrt_rec709_full_100nits in aces 0.7.1
                _ocio->setInputColorspace("rrt_rec709_full_100nits");
            } else if (_ocio->hasColorspace("rrt_rec709")) {
                // rrt_rec709 in aces 0.1.1
                _ocio->setInputColorspace("rrt_rec709");
            } else if (_ocio->hasColorspace("hd10")) {
                // hd10 in spi-anim and spi-vfx
                _ocio->setInputColorspace("hd10");
            }
        } else if(!strcmp(colorSpaceStr, "KodakLog")) {
            if (_ocio->hasColorspace("Cineon")) {
                // Cineon in nuke-default
                _ocio->setInputColorspace("Cineon");
            } else if (_ocio->hasColorspace("REDlogFilm")) {
                // REDlogFilm in aces 1.0.0
                _ocio->setInputColorspace("REDlogFilm");
            } else if (_ocio->hasColorspace("cineon")) {
                // cineon in aces 0.7.1
                _ocio->setInputColorspace("cineon");
            } else if (_ocio->hasColorspace("adx10")) {
                // adx10 in aces 0.1.1
                _ocio->setInputColorspace("adx10");
            } else if (_ocio->hasColorspace("lg10")) {
                // lg10 in spi-vfx
                _ocio->setInputColorspace("lg10");
            } else if (_ocio->hasColorspace("lm10")) {
                // lm10 in spi-anim
                _ocio->setInputColorspace("lm10");
            } else {
                _ocio->setInputColorspace("compositing_log");
            }
        } else if(!strcmp(colorSpaceStr, "Linear")) {
            _ocio->setInputColorspace("scene_linear");
            // lnf in spi-vfx
        } else if (_ocio->hasColorspace(colorSpaceStr)) {
            // maybe we're lucky
            _ocio->setInputColorspace(colorSpaceStr);
        } else {
            // unknown color-space or Linear, don't do anything
        }
    }

# endif // OFX_IO_USING_OCIO

    switch (_subImagesSpec[0].nchannels) {
        case 0:
            *components = OFX::ePixelComponentNone;
            *componentCount = _subImagesSpec[0].nchannels;
            break;
        case 1:
            *components = OFX::ePixelComponentAlpha;
            *componentCount = _subImagesSpec[0].nchannels;
            break;
        case 3:
            *components = OFX::ePixelComponentRGB;
            *componentCount = _subImagesSpec[0].nchannels;
           break;
        case 4:
            *components = OFX::ePixelComponentRGBA;
            *componentCount = _subImagesSpec[0].nchannels;
           break;
        default:
            *components = OFX::ePixelComponentRGBA;
            *componentCount = 4;
          break;
    }
    *componentCount = _subImagesSpec[0].nchannels;
    
#ifdef OFX_READ_OIIO_NEWMENU
    // rebuild the channel choices
    buildChannelMenus();
    // set the default values for R, G, B, A channels
    setDefaultChannels(components);

    setChannels();
#else
    _firstChannel->setDisplayRange(0, _subImagesSpec[0].nchannels);
    // set the first channel to the alpha channel if output is alpha
    if (_subImagesSpec[0].alpha_channel != -1 && *components == OFX::ePixelComponentAlpha) {
        _firstChannel->setValue(_subImagesSpec[0].alpha_channel);
    }
#endif
    if (*components != OFX::ePixelComponentRGBA && *components != OFX::ePixelComponentAlpha) {
        *premult = OFX::eImageOpaque;
    } else {
        bool unassociatedAlpha = _subImagesSpec[0].get_int_attribute("oiio:UnassociatedAlpha", 0);
        if (unassociatedAlpha) {
            *premult = OFX::eImageUnPreMultiplied;
        } else {
            *premult = OFX::eImagePreMultiplied;
        }
    }

}

void
ReadOIIOPlugin::openFile(const std::string& filename, bool useCache, ImageInput** img, ImageSpec* spec, int subimage)
{
#ifdef OFX_READ_OIIO_USES_CACHE
    if (useCache) {
        //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
        if(!_cache->get_imagespec(ustring(filename), *spec, subimage)){
            setPersistentMessage(OFX::Message::eMessageError, "", _cache->geterror());
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
        
#pragma message WARN("Uncomment this block when https://github.com/OpenImageIO/oiio/issues/1239 (race-condition) is REALLY fixed")
        /*{
            //Keep in OIIO cache only the current frame in case multiple threads try to access it
            OFX::MultiThread::AutoMutex l(_lastFileReadNoPlaybackMutex);
            if (!_lastFileReadNoPlayback.empty() && filename != _lastFileReadNoPlayback) {
                _cache->invalidate(ustring(_lastFileReadNoPlayback));
            }
            _lastFileReadNoPlayback = filename;
        }*/
        
    } else // warning: '{' must follow #endif
#endif
    { // !useCache
        // Always keep unassociated alpha.
        // Don't let OIIO premultiply, because if the image is 8bits,
        // it multiplies in 8bits (see TIFFInput::unassalpha_to_assocalpha()),
        // which causes a lot of precision loss.
        // see also https://github.com/OpenImageIO/oiio/issues/960
        ImageSpec config;
        config.attribute("oiio:UnassociatedAlpha", 1);
        
        *img = ImageInput::open(filename, &config);
        if (!(*img)) {
            setPersistentMessage(OFX::Message::eMessageError, "", std::string("ReadOIIO: cannot open file ") + filename);
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;
        }
        if (!(*img)->seek_subimage(subimage, 0, *spec)) {
            std::stringstream ss;
            ss << "ReadOIIO: cannot seek subimage " << subimage << " in " << filename;
            setPersistentMessage(OFX::Message::eMessageError, "", ss.str());
            OFX::throwSuiteStatusException(kOfxStatFailed);
            return;

        }
    }

}

void ReadOIIOPlugin::decodePlane(const std::string& filename, OfxTime time, bool isPlayback, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds,
                                 OFX::PixelComponentEnum pixelComponents, int pixelComponentCount, const std::string& rawComponents, int rowBytes)
{
    
    //assert(kSupportsTiles || (renderWindow.x1 == 0 && renderWindow.x2 == spec.full_width && renderWindow.y1 == 0 && renderWindow.y2 == spec.full_height));
    //assert((renderWindow.x2 - renderWindow.x1) <= spec.width && (renderWindow.y2 - renderWindow.y1) <= spec.height);
    assert(bounds.x1 <= renderWindow.x1 && renderWindow.x1 <= renderWindow.x2 && renderWindow.x2 <= bounds.x2);
    assert(bounds.y1 <= renderWindow.y1 && renderWindow.y1 <= renderWindow.y2 && renderWindow.y2 <= bounds.y2);
    
    // we only support RGBA, RGB or Alpha output clip on the color plane
    if (pixelComponents != OFX::ePixelComponentRGBA && pixelComponents != OFX::ePixelComponentRGB && pixelComponents != OFX::ePixelComponentAlpha
        && pixelComponents != OFX::ePixelComponentCustom) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OIIO: can only read RGBA, RGB, Alpha or custom components images");
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
        return;
    }
    
#ifdef OFX_READ_OIIO_USES_CACHE
    bool useCache = !isPlayback;
#else 
    bool useCache = false;
#endif
    
    std::auto_ptr<ImageInput> img;
    ImageSpec spec;
    if (!useCache) {
        ImageInput* rawImg = 0;
        openFile(filename, useCache, &rawImg, & spec, 0);
        img.reset(rawImg);
    }
    
#ifndef OFX_READ_OIIO_NEWMENU
    decodePlaneNoShuffleMenu(img.get(), spec, filename, time, isPlayback, renderWindow, pixelData, bounds, pixelComponents, rowBytes);
#else
    
   
    std::vector<ImageSpec> specs;
    {
        int subImageIndex = 0;
#ifdef OFX_READ_OIIO_USES_CACHE
        if (useCache) {
#ifdef OFX_READ_OIIO_SUPPORTS_SUBIMAGES
            while (_cache->get_imagespec(ustring(filename), spec,subImageIndex, 0)) {
                specs.push_back(spec);
                ++subImageIndex;
            }
#else
            if (_cache->get_imagespec(ustring(filename), spec,subImageIndex, 0)) {
                specs.push_back(spec);
            }
#endif
        } else // !useCache
#endif
        {
            assert(img.get());
#ifdef OFX_READ_OIIO_SUPPORTS_SUBIMAGES
            while (img->seek_subimage(subImageIndex, 0, spec)) {
                specs.push_back(spec);
                ++subImageIndex;
            }
#else
            if (img->seek_subimage(subImageIndex, 0, spec)) {
                specs.push_back(spec);
            }
#endif
        }
    }
    
    std::vector<int> channels;
    int numChannels = 0;
    int pixelBytes = 0;
    if (pixelComponents != OFX::ePixelComponentCustom) {
        assert(rawComponents == kOfxImageComponentAlpha || rawComponents == kOfxImageComponentRGB || rawComponents == kOfxImageComponentRGBA);
        int rChannel, gChannel, bChannel, aChannel;
        _rChannel->getValueAtTime(time, rChannel);
        _gChannel->getValueAtTime(time, gChannel);
        _bChannel->getValueAtTime(time, bChannel);
        _aChannel->getValueAtTime(time, aChannel);
        
        pixelBytes = pixelComponentCount * getComponentBytes(OFX::eBitDepthFloat);
        
        switch (pixelComponents) {
            case OFX::ePixelComponentRGBA:
                numChannels = 4;
                channels.resize(numChannels);
                channels[0] = rChannel;
                channels[1] = gChannel;
                channels[2] = bChannel;
                channels[3] = aChannel;
                break;
            case OFX::ePixelComponentRGB:
                numChannels = 3;
                channels.resize(numChannels);
                channels[0] = rChannel;
                channels[1] = gChannel;
                channels[2] = bChannel;
                break;
            case OFX::ePixelComponentAlpha:
                numChannels = 1;
                channels.resize(numChannels);
                channels[0] = aChannel;
                break;
            default:
                assert(false);
                break;
        }
    } // if (pixelComponents != OFX::ePixelComponentCustom) {
#ifdef OFX_EXTENSIONS_NATRON
    else {
        std::vector<std::string> layerChannels = OFX::mapPixelComponentCustomToLayerChannels(rawComponents);
        if (!layerChannels.empty()) {
            numChannels = (int)layerChannels.size() - 1;
            channels.resize(numChannels);
            std::string layer = layerChannels[0];

            pixelBytes = numChannels * sizeof(float);
            if (numChannels == 1 && layerChannels[1] == layer) {
                layer.clear();
            }
            for (int i = 0; i < numChannels; ++i) {
                bool found = false;
                for (std::size_t j = 0; j < spec.channelnames.size(); ++j) {
                    std::string realChan;
                    if (!layer.empty()) {
                        realChan.append(layer);
                        realChan.push_back('.');
                    }
                    realChan.append(layerChannels[i+1]);
                    if (spec.channelnames[j] == realChan) {
                        channels[i] = j + kXChannelFirst;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    setPersistentMessage(OFX::Message::eMessageError, "", "Could not find channel named " + layerChannels[i+1]);
                    OFX::throwSuiteStatusException(kOfxStatFailed);
                    return;
                }
            }
        }
    }
#endif
    
    //For each subimage, a set of channels
    std::map<int, std::vector<int> > subImageChannels;
    
    int nChannelsVisited = 0;
    {
        std::list<int> visitedChannels;
        int subImageIndex = 0;
        for (std::vector<ImageSpec>::iterator it = specs.begin(); it != specs.end(); ++it) {
            for (std::size_t i = 0; i < channels.size(); ++i) {
                if (std::find(visitedChannels.begin(), visitedChannels.end(), i) != visitedChannels.end()) {
                    continue;
                }
                if (channels[i] >= nChannelsVisited + kXChannelFirst && channels[i] < nChannelsVisited + kXChannelFirst + it->nchannels) {
                    subImageChannels[subImageIndex].push_back(channels[i] - nChannelsVisited);
                    visitedChannels.push_back(i);
                }
            }
            ++subImageIndex;
            nChannelsVisited += it->nchannels;
        }
    }
    
    size_t pixelDataOffset = (size_t)(renderWindow.y1 - bounds.y1) * rowBytes + (size_t)(renderWindow.x1 - bounds.x1) * pixelBytes;

#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
    bool useDisplayWindowOrigin;
    _useDisplayWindowAsOrigin->getValue(useDisplayWindowOrigin);
#else
    bool useDisplayWindowOrigin = true;
#endif
    


    for (std::map<int, std::vector<int> >::iterator subImageit = subImageChannels.begin();
         subImageit != subImageChannels.end(); ++subImageit) {
        
        const std::vector<int>& subImageChannels = subImageit->second;
        
        std::size_t incr; // number of channels processed
        for (std::size_t i = 0; i < subImageChannels.size(); i+=incr) {
            incr = 1;
            if (subImageChannels[i] < kXChannelFirst) {
                // fill channel with constant value
                char* lineStart = (char*)pixelData + pixelDataOffset; // (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
                for (int y = renderWindow.y1; y < renderWindow.y2; ++y, lineStart += rowBytes) {
                    float *cur = (float*)lineStart;
                    for (int x = renderWindow.x1; x < renderWindow.x2; ++x, cur += numChannels) {
                        cur[i] = float(subImageChannels[i]);
                    }
                }
            } else {
                
                // read as many contiguous channels as we can
                while ((i+incr) < subImageChannels.size() &&
                       subImageChannels[i+incr] == subImageChannels[i+incr-1]+1) {
                    ++incr;
                }
                const int outputChannelBegin = i;
                const int chbegin = subImageChannels[i] - kXChannelFirst; // start channel for reading
                const int chend = chbegin + incr; // last channel + 1
                size_t pixelDataOffset2 = (size_t)(renderWindow.y2 - 1 - bounds.y1) * rowBytes + (size_t)(renderWindow.x1 - bounds.x1) * pixelBytes; // offset for line y2-1
                
#         ifdef OFX_READ_OIIO_USES_CACHE
                if (useCache) {
                    if (!_cache->get_pixels(ustring(filename),
                                            subImageit->first, //subimage
                                            0, //miplevel
                                            useDisplayWindowOrigin ? spec.full_x + renderWindow.x1 : renderWindow.x1, //x begin
                                            useDisplayWindowOrigin ? spec.full_x + renderWindow.x2 : renderWindow.x2, //x end
                                            useDisplayWindowOrigin ? spec.full_y + spec.full_height - renderWindow.y2 : renderWindow.y2, //y begin
                                            useDisplayWindowOrigin ? spec.full_y + spec.full_height - renderWindow.y1 : renderWindow.y1, //y end
                                            0, //z begin
                                            1, //z end
                                            chbegin, //chan begin
                                            chend, // chan end
                                            TypeDesc::FLOAT, // data type
                                            //(float*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y2 - 1) + outputChannelBegin,// output buffer
                                            (float*)((char*)pixelData + pixelDataOffset2) + outputChannelBegin,// output buffer
                                            numChannels * sizeof(float), //x stride
                                            -rowBytes, //y stride < make it invert Y
                                            AutoStride //z stride
#                                     if OIIO_VERSION >= 10605
                                            ,
                                            chbegin, // only cache these channels
                                            chend
#                                     endif
                                            )) {
                        setPersistentMessage(OFX::Message::eMessageError, "", _cache->geterror());
                        return;
                    }
                } else // warning: '{' must follow #endif
#         endif
                { // !useCache
                    
                    if (!img->seek_subimage(subImageit->first, 0, spec)) {
                        setPersistentMessage(OFX::Message::eMessageError, "", img->geterror());
                        return;
                    }
                    
                    assert(kSupportsTiles || (!kSupportsTiles && (renderWindow.x2 - renderWindow.x1) == spec.width && (renderWindow.y2 - renderWindow.y1) == spec.height));
                    if (spec.tile_width == 0) {
                        ///read by scanlines
                        img->read_scanlines(spec.height - renderWindow.y2, //y begin
                                            spec.height - renderWindow.y1, //y end
                                            0, // z
                                            chbegin, // chan begin
                                            chend, // chan end
                                            TypeDesc::FLOAT, // data type
                                            (float*)((char*)pixelData + pixelDataOffset2) + outputChannelBegin,
                                            numChannels * sizeof(float), //x stride
                                            -rowBytes); //y stride < make it invert Y;
                    } else {
                        img->read_tiles(renderWindow.x1, //x begin
                                        renderWindow.x2,//x end
                                        spec.height - renderWindow.y2,//y begin
                                        spec.height - renderWindow.y1,//y end
                                        0, // z begin
                                        1, // z end
                                        chbegin, // chan begin
                                        chend, // chan end
                                        TypeDesc::FLOAT,  // data type
                                        (float*)((char*)pixelData + pixelDataOffset2) + outputChannelBegin,
                                        numChannels * sizeof(float), //x stride
                                        -rowBytes, //y stride < make it invert Y
                                        AutoStride); //z stride
                    }
                } // !useCache
            } // if (subImageChannels[i] < kXChannelFirst) {
        } // for (std::size_t i = 0; i < subImageChannels(); i+=incr) {
    } //for (std::map<int, std::vector<int> >::iterator subImageit = subImageChannels.begin(); subImageit != subImageChannels.end(); ++subImageit) {

#endif // OFX_READ_OIIO_NEWMENU
    if (!useCache) {
        img->close();
    }
    
}

#ifndef OFX_READ_OIIO_NEWMENU
void
ReadOIIOPlugin::decodePlaneNoShuffleMenu(ImageInput* img, const ImageSpec& spec,  const std::string& filename, OfxTime time, bool isPlayback, const OfxRectI& renderWindow, float *pixelData, const OfxRectI& bounds,OFX::PixelComponentEnum pixelComponents, int rowBytes)
{
#ifdef OFX_READ_OIIO_USES_CACHE
    bool useCache = !isPlayback;
#endif
    int firstChannel;
    _firstChannel->getValueAtTime(time, firstChannel);
    
    int chcount = spec.nchannels - firstChannel; // number of available channels
    if (chcount <= 0) {
        std::ostringstream oss;
        oss << "ReadOIIO: Cannot read, first channel is " << firstChannel << ", but image has only " << spec.nchannels << " channels";
        setPersistentMessage(OFX::Message::eMessageError, "", oss.str());
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        return;
    }
    int numChannels = 0;
    int outputChannelBegin = 0;
    int chbegin; // start channel for reading
    int chend; // last channel + 1
    bool fillRGB = false;
    bool fillAlpha = false;
    bool moveAlpha = false;
    bool copyRtoGB = false;
    switch (pixelComponents) {
        case OFX::ePixelComponentRGBA:
            numChannels = 4;
            if (chcount == 1) {
                // only one channel to read from input
                chbegin = firstChannel;
                chend = firstChannel + 1;
                if (spec.alpha_channel == -1 || spec.alpha_channel != firstChannel) {
                    // Most probably a luminance image.
                    // fill alpha with 0, duplicate the single channel to r,g,b
                    fillAlpha = true;
                    copyRtoGB = true;
                } else {
                    // An alpha image.
                    fillRGB = true;
                    fillAlpha = false;
                    outputChannelBegin = 3;
                }
            } else {
                chbegin = firstChannel;
                chend = std::min(spec.nchannels, firstChannel + numChannels);
                // After reading, if spec.alpha_channel != 3 and -1,
                // move the channel spec.alpha_channel to channel 3 and fill it
                // with zeroes
                moveAlpha = (firstChannel <= spec.alpha_channel && spec.alpha_channel < firstChannel+3);
                fillAlpha = (chcount < 4); //(spec.alpha_channel == -1);
                
                fillRGB = (chcount < 3); // need to fill B with black
            }
            break;
        case OFX::ePixelComponentRGB:
            numChannels = 3;
            fillRGB = (spec.nchannels == 1) || (spec.nchannels == 2);
            if (chcount == 1) {
                chbegin = chend = -1;
            } else {
                chbegin = firstChannel;
                chend = std::min(spec.nchannels, firstChannel + numChannels);
            }
            break;
        case OFX::ePixelComponentAlpha:
            numChannels = 1;
            chbegin = firstChannel;
            chend = chbegin + numChannels;
            break;
        default:
#         ifdef OFX_READ_OIIO_USES_CACHE
            if (!useCache) // warning: '{' must follow #endif
#         endif
            {
                img->close();
            }
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
            return;
    }
    assert(numChannels);
    int pixelBytes = numChannels * sizeof(float);
    size_t pixelDataOffset = (size_t)(renderWindow.y1 - bounds.y1) * rowBytes + (size_t)(renderWindow.x1 - bounds.x1) * pixelBytes;
    
    if (fillRGB) {
        // fill RGB values with black
        assert(pixelComponents != OFX::ePixelComponentAlpha);
        char* lineStart = (char*)pixelData + pixelDataOffset; // (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
        for (int y = renderWindow.y1; y < renderWindow.y2; ++y, lineStart += rowBytes) {
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
        char* lineStart = (char*)pixelData + pixelDataOffset; // (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
        for (int y = renderWindow.y1; y < renderWindow.y2; ++y, lineStart += rowBytes) {
            float *cur = (float*)lineStart + outputChannelAlpha;
            for (int x = renderWindow.x1; x < renderWindow.x2; ++x, cur += numChannels) {
                cur[0] = 1.;
            }
        }
    }
    
    if (chbegin != -1 && chend != -1) {
        assert(0 <= chbegin && chbegin < spec.nchannels && chbegin < chend && 0 < chend && chend <= spec.nchannels);
        size_t pixelDataOffset2 = (size_t)(renderWindow.y2 - 1 - bounds.y1) * rowBytes + (size_t)(renderWindow.x1 - bounds.x1) * pixelBytes;
        
#     ifdef OFX_READ_OIIO_USES_CACHE
        if (useCache) {
            // offset for line y2-1
            if (!_cache->get_pixels(ustring(filename),
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
                                    //(float*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y2 - 1) + outputChannelBegin,// output buffer
                                    (float*)((char*)pixelData + pixelDataOffset2)
                                    + outputChannelBegin,// output buffer
                                    numChannels * sizeof(float), //x stride
                                    -rowBytes, //y stride < make it invert Y
                                    AutoStride //z stride
                                    )) {
                setPersistentMessage(OFX::Message::eMessageError, "", _cache->geterror());
                return;
            }
        } else // warning: '{' must follow #endif
#     endif // OFX_READ_OIIO_USES_CACHE
        { // !useCache
            assert(kSupportsTiles || (!kSupportsTiles && (renderWindow.x2 - renderWindow.x1) == spec.width && (renderWindow.y2 - renderWindow.y1) == spec.height));
            if (spec.tile_width == 0) {
                ///read by scanlines
                img->read_scanlines(spec.height - renderWindow.y2, //y begin
                                    spec.height - renderWindow.y1, //y end
                                    0, // z
                                    chbegin, // chan begin
                                    chend, // chan end
                                    TypeDesc::FLOAT, // data type
                                    (float*)((char*)pixelData + pixelDataOffset2) + outputChannelBegin,
                                    numChannels * sizeof(float), //x stride
                                    -rowBytes); //y stride < make it invert Y;
            } else {
                img->read_tiles(renderWindow.x1, //x begin
                                renderWindow.x2,//x end
                                spec.height - renderWindow.y2,//y begin
                                spec.height - renderWindow.y1,//y end
                                0, // z begin
                                1, // z end
                                chbegin, // chan begin
                                chend, // chan end
                                TypeDesc::FLOAT,  // data type
                                (float*)((char*)pixelData + pixelDataOffset2) + outputChannelBegin,
                                numChannels * sizeof(float), //x stride
                                -rowBytes, //y stride < make it invert Y
                                AutoStride); //z stride
            }
            img->close();
        }
    }
    if (moveAlpha) {
        // move alpha channel to the right place
        assert(pixelComponents == OFX::ePixelComponentRGB && spec.alpha_channel < 3 && spec.alpha_channel != -1);
        char* lineStart = (char*)pixelData + pixelDataOffset; // (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
        for (int y = renderWindow.y1; y < renderWindow.y2; ++y, lineStart += rowBytes) {
            float *cur = (float*)lineStart;
            for (int x = renderWindow.x1; x < renderWindow.x2; ++x, cur += numChannels) {
                cur[3] = cur[spec.alpha_channel];
                cur[spec.alpha_channel] = 0.;
            }
        }
    }
    if (copyRtoGB) {
        // copy red to green and blue RGB values with black
        assert(pixelComponents != OFX::ePixelComponentAlpha);
        char* lineStart = (char*)pixelData + pixelDataOffset; // (char*)dstImg->getPixelAddress(renderWindow.x1, renderWindow.y1);
        for (int y = renderWindow.y1; y < renderWindow.y2; ++y, lineStart += rowBytes) {
            float *cur = (float*)lineStart;
            for (int x = renderWindow.x1; x < renderWindow.x2; ++x, cur += numChannels) {
                cur[1] = cur[2] = cur[0];
            }
        }
    }

}
#endif //!OFX_READ_OIIO_NEWMENU

bool
ReadOIIOPlugin::getFrameBounds(const std::string& filename,
                               OfxTime /*time*/,
                               OfxRectI *bounds,
                               double *par,
                               std::string *error)
{
    assert(bounds && par);
#ifdef OFX_READ_OIIO_USES_CACHE
    ImageSpec spec;
    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    if(!_cache->get_imagespec(ustring(filename), spec)) {
        if (error) {
            *error = _cache->geterror();
        }
        return false;
    }
#else 
    std::auto_ptr<ImageInput> img(ImageInput::open(filename));
    if (!img.get()) {
        if (error) {
            *error = std::string("ReadOIIO: cannot open file ") + filename;
        }
        return false;
    }
    const ImageSpec &spec = img->spec();
#endif
    
#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
    bool originAtDisplayWindow;
    _useDisplayWindowAsOrigin->getValue(originAtDisplayWindow);
#else
    bool originAtDisplayWindow = true;
#endif
    
    if (originAtDisplayWindow) {
        // the image coordinates are expressed in the "full/display" image.
        // The RoD are the coordinates of the data window with respect to that full window
        bounds->x1 = (spec.x - spec.full_x);
        bounds->x2 = (spec.x + spec.width - spec.full_x);
        bounds->y1 = spec.full_y + spec.full_height - (spec.y + spec.height);
        bounds->y2 = (spec.full_height) + (spec.full_y - spec.y);
    } else {
        bounds->x1 = spec.x;
        bounds->x2 = spec.x + spec.width;
        bounds->y1 =  spec.y;
        bounds->y2 =  spec.y + spec.height;
    }
    *par = spec.get_float_attribute("PixelAspectRatio", 1);
#ifdef OFX_READ_OIIO_USES_CACHE
#else
    img->close();
#endif
    return true;
}

std::string
ReadOIIOPlugin::metadata(const std::string& filename)
{
    std::stringstream ss;

#ifdef OFX_READ_OIIO_USES_CACHE
    ImageSpec spec;
    //use the thread-safe version of get_imagespec (i.e: make a copy of the imagespec)
    if(!_cache->get_imagespec(ustring(filename), spec)){
        setPersistentMessage(OFX::Message::eMessageError, "", _cache->geterror());
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return std::string();
    }
#else 
    std::auto_ptr<ImageInput> img(ImageInput::open(filename));
    if (!img.get()) {
        setPersistentMessage(OFX::Message::eMessageError, "", std::string("ReadOIIO: cannot open file ") + filename);
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return std::string();
    }
    const ImageSpec& spec = img->spec();
#endif
    ss << "file: " << filename << std::endl;
    ss << "    channel list: ";
    for (int i = 0;  i < spec.nchannels;  ++i) {
        ss << i << ":";
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
#ifdef OFX_READ_OIIO_USES_CACHE
#else
    img->close();
#endif

    return ss.str();
}


using namespace OFX;

mDeclareReaderPluginFactory(ReadOIIOPluginFactory, ;, ;,false);

void ReadOIIOPluginFactory::load() {
}

void ReadOIIOPluginFactory::unload()
{
#  ifdef OFX_READ_OIIO_SHARED_CACHE
    // get the shared image cache (may be shared with other plugins using OIIO)
    ImageCache* sharedcache = ImageCache::create(true);
    // purge it
    // teardown is dangerous if there are other users
    ImageCache::destroy(sharedcache);
#  endif
}

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
    GenericReaderDescribe(desc, kSupportsTiles, kIsMultiPlanar);


    std::string extensions_list;
    getattribute("extension_list", extensions_list);

    std::string extensions_pretty;
    {
        std::stringstream formatss(extensions_list);
        std::string format;
        std::vector<std::string> extensions;
        while (std::getline(formatss, format, ';')) {
            std::stringstream extensionss(format);
            std::string extension;
            std::getline(extensionss, extension, ':'); // extract the format
            extensions_pretty += extension;
            extensions_pretty += ": ";
            bool first = true;
            while (std::getline(extensionss, extension, ',')) {
                if (!first) {
                    extensions_pretty += ", ";
                }
                first = false;
                extensions_pretty += extension;
            }
            extensions_pretty += "; ";
        }
    }

    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginDescription(kPluginDescription
                              "\n\n"
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
                              "All supported formats and extensions: " + extensions_pretty + "\n\n"
                              + oiio_versions());


#ifdef OFX_EXTENSIONS_TUTTLE
#if 0
    // hard-coded extensions list
    const char* extensions[] = { "bmp", "cin", "dds", "dpx", "f3d", "fits", "hdr", "ico",
        "iff", "jpg", "jpe", "jpeg", "jif", "jfif", "jfi", "jp2", "j2k", "exr", "png",
        "pbm", "pgm", "ppm",
#     if OIIO_VERSION >= 10400
        "pfm",
#     endif
        "psd", "pdd", "psb", "ptex", "rla", "sgi", "rgb", "rgba", "bw", "int", "inta", "pic", "tga", "tpic", "tif", "tiff", "tx", "env", "sm", "vsm", "zfile", NULL };
#else
    // get extensions from OIIO (but there is no distinctions between readers and writers)
    std::vector<std::string> extensions;
    {
        std::stringstream formatss(extensions_list);
        std::string format;
        while (std::getline(formatss, format, ';')) {
            std::stringstream extensionss(format);
            std::string extension;
            std::getline(extensionss, extension, ':'); // extract the format
            while (std::getline(extensionss, extension, ',')) {
                extensions.push_back(extension);
            }
        }
    }

#endif
    desc.addSupportedExtensions(extensions);
    desc.setPluginEvaluation(91);
#endif

}

static void
appendDefaultChannelList(ChoiceParamDescriptor *channel)
{
    channel->appendOption("0");
    channel->appendOption("1");
    for (int i = 0; i < kDefaultChannelCount; ++i) {
        std::ostringstream oss;
        oss << "channel " << i;
        channel->appendOption(oss.str());
    }
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void ReadOIIOPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    gHostIsNatron = (OFX::getImageEffectHostDescription()->isNatron);

    // make some pages and to things in
    PageParamDescriptor *page = GenericReaderDescribeInContextBegin(desc, context, isVideoStreamPlugin(), kSupportsRGBA, kSupportsRGB, kSupportsAlpha, kSupportsTiles);

    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamShowMetadata);
        param->setLabel(kParamShowMetadataLabel);
        param->setHint(kParamShowMetadataHint);
        page->addChild(*param);
    }

#ifndef OFX_READ_OIIO_NEWMENU
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFirstChannel);
        param->setLabel(kParamFirstChannelLabel);
        param->setHint(kParamFirstChannelHint);
        page->addChild(*param);
    }
#endif


#ifdef OFX_READ_OIIO_NEWMENU
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamRChannel);
        param->setLabel(kParamRChannelLabel);
        param->setHint(kParamRChannelHint);
        appendDefaultChannelList(param);
        param->setAnimates(true);
        param->setIsPersistant(false); //don't save, we will restore it using the StringParams holding the index
        page->addChild(*param);
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamGChannel);
        param->setLabel(kParamGChannelLabel);
        param->setHint(kParamGChannelHint);
        appendDefaultChannelList(param);
        param->setAnimates(true);
        param->setIsPersistant(false); //don't save, we will restore it using the StringParams holding the index
        page->addChild(*param);
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamBChannel);
        param->setLabel(kParamBChannelLabel);
        param->setHint(kParamBChannelHint);
        appendDefaultChannelList(param);
        param->setAnimates(true);
        param->setIsPersistant(false); //don't save, we will restore it using the StringParams holding the index
        page->addChild(*param);
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamAChannel);
        param->setLabel(kParamAChannelLabel);
        param->setHint(kParamAChannelHint);
        appendDefaultChannelList(param);
        param->setAnimates(true);
        param->setDefault(1); // opaque by default
        param->setIsPersistant(false); //don't save, we will restore it using the StringParams holding the index
        page->addChild(*param);
    }
    
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamRChannelName);
        param->setLabel(kParamRChannelLabel);
        param->setHint(kParamRChannelHint);
        param->setAnimates(false);
        param->setIsSecret(true); // never meant to be visible
        page->addChild(*param);
    }
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamGChannelName);
        param->setLabel(kParamGChannelLabel);
        param->setHint(kParamGChannelHint);
        param->setAnimates(false);
        param->setIsSecret(true); // never meant to be visible
        page->addChild(*param);
    }

    {
        StringParamDescriptor* param = desc.defineStringParam(kParamBChannelName);
        param->setLabel(kParamBChannelLabel);
        param->setHint(kParamBChannelHint);
        param->setAnimates(false);
        param->setIsSecret(true); // never meant to be visible
        page->addChild(*param);
    }

    {
        StringParamDescriptor* param = desc.defineStringParam(kParamAChannelName);
        param->setLabel(kParamAChannelLabel);
        param->setHint(kParamAChannelHint);
        param->setAnimates(false);
        param->setIsSecret(true); // never meant to be visible
        page->addChild(*param);
    }

#endif

#ifdef USE_READ_OIIO_PARAM_USE_DISPLAY_WINDOW
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamUseDisplayWindowAsOrigin);
        param->setLabel(kParamUseDisplayWindowAsOriginLabel);
        param->setHint(kParamUseDisplayWindowAsOriginHint);
        param->setDefault(true);
        page->addChild(*param);
    }
#endif
    GenericReaderDescribeInContextEnd(desc, context, page, "reference", "reference");
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* ReadOIIOPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    ReadOIIOPlugin* ret =  new ReadOIIOPlugin(handle);
    ret->restoreStateFromParameters();
    return ret;
}

void getReadOIIOPluginID(OFX::PluginFactoryArray &ids)
{
    static ReadOIIOPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

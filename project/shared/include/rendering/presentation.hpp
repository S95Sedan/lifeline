#pragma once
#include "rendering/image.hpp"
namespace lifeline {
// Native framebuffer presentation. History is the display image, independent
// of the scene texture and script drawing batches.
class Presentation {
    AssetImage output_{"Presented frame",512,448,Bytes(512*448*4)};
public:
    void seed(const AssetImage& image){output_=image;}
    const AssetImage& compose(const AssetImage&,int mode,bool preserve_resolution=false);
};
}

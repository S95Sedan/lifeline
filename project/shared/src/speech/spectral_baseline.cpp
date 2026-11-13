#include "speech/spectral_baseline.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace lifeline {
SpeechSpectralBaseline::SpeechSpectralBaseline(SpectralProjectionData data)
    : data_(std::move(data)) {
    const auto rows=data_.bands, columns=data_.columns;
    if(rows<2 || rows>256 || columns<2 || columns>rows ||
       data_.basis.size()!=std::size_t(rows)*columns ||
       data_.qr.size()!=data_.basis.size() || data_.reflectors.size()!=columns)
        throw std::invalid_argument("Invalid speech projection dimensions");
    for(const auto* values:{&data_.basis,&data_.qr,&data_.reflectors})
        for(float value:*values)
            if(!std::isfinite(value)) throw std::invalid_argument("Nonfinite speech projection data");
    for(unsigned i=0;i<columns;++i)
        if(data_.qr[std::size_t(i)*rows+i]==0)
            throw std::invalid_argument("Singular speech projection data");
}

std::vector<float> SpeechSpectralBaseline::project(std::span<const float> bands) const {
    const auto rows=data_.bands;
    if(bands.size()!=rows) throw std::invalid_argument("Wrong speech projection band count");
    for(float value:bands)
        if(!std::isfinite(value)) throw std::invalid_argument("Nonfinite speech projection input");
    std::vector<float> rhs(bands.begin(),bands.end()), qr=data_.qr;
    const float ceiling=*std::max_element(rhs.begin(),rhs.end());
    // 0x2942a8 retains the absolute level; this is not mean subtraction.
    for(auto& value:rhs) {
        const float difference=value-ceiling;
        const float exponential=std::exp(float(double(difference)*2.0));
        const float correction=float(double(std::log(1.0f+exponential))*0.5);
        value-=correction;
    }
    unsigned count=data_.columns;
    for(unsigned k=0;k<count;++k) {
        const auto base=std::size_t(k)*rows;
        float dot=rhs[k];
        for(unsigned r=k+1;r<rows;++r) dot+=qr[base+r]*rhs[r];
        dot*=data_.reflectors[k];
        rhs[k]-=dot;
        for(unsigned r=k+1;r<rows;++r) rhs[r]-=dot*qr[base+r];
    }
    std::vector<unsigned> indices(count);
    std::iota(indices.begin(),indices.end(),0u);
    std::vector<float> coefficients(count);
    for(;;) {
        unsigned rejected=count;
        for(unsigned pos=count;pos>0;) {
            const unsigned i=--pos;
            float value=rhs[i];
            for(unsigned j=i+1;j<count;++j) value-=qr[std::size_t(j)*rows+i]*coefficients[j];
            coefficients[i]=value/qr[std::size_t(i)*rows+i];
            if(i>=2 && coefficients[i]>0) { rejected=i; break; }
        }
        if(rejected==count) break;
        // 0x293e40 deletes the offending column and rotates the remaining
        // triangular system and RHS together. Re-solving from scratch changes
        // the original elimination order and its numerical behavior.
        for(unsigned j=rejected+1;j<count;++j) {
            const auto base=std::size_t(j)*rows;
            const float a=qr[base+j-1], b=qr[base+j];
            float c,s;
            if(std::abs(a)<std::abs(b)) {
                const float t=-a/b;
                s=float(1.0/double(std::sqrt(1.0f+t*t))); c=s*t;
            } else {
                if(a==0) throw std::runtime_error("Singular reduced speech projection");
                const float t=-b/a;
                c=float(1.0/double(std::sqrt(1.0f+t*t))); s=c*t;
            }
            for(unsigned r=0;r+1<j;++r) qr[base-rows+r]=qr[base+r];
            qr[base-rows+j-1]=c*a-s*b;
            for(unsigned k=j+1;k<count;++k) {
                const auto upper=std::size_t(k)*rows+j-1;
                const float x=qr[upper],y=qr[upper+1];
                qr[upper]=c*x-s*y; qr[upper+1]=s*x+c*y;
            }
            const float x=rhs[j-1], y=rhs[j];
            rhs[j-1]=c*x-s*y; rhs[j]=s*x+c*y;
        }
        indices.erase(indices.begin()+rejected);
        --count;
    }
    std::fill(rhs.begin(),rhs.end(),0.0f);
    for(unsigned k=0;k<count;++k)
        for(unsigned r=0;r<rows;++r)
            rhs[r]+=data_.basis[std::size_t(indices[k])*rows+r]*coefficients[k];
    return rhs;
}
}

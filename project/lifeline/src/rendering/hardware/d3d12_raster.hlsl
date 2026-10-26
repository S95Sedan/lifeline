// Ordered tile rasterization preserves material order. Quality mode assigns
// shared edges to one triangle; reference mode retains inclusive CPU coverage.
struct Vertex {float4 screen,uv,color,shadow;};
struct Triangle {Vertex a,b,c;uint4 texInfo,blend;float4 fog,parameters;};
StructuredBuffer<Triangle> triangles : register(t0);
StructuredBuffer<uint> texels : register(t1);
StructuredBuffer<uint> tiles : register(t2);
StructuredBuffer<uint> capture : register(t3);
StructuredBuffer<float> sceneDepth : register(t4);
StructuredBuffer<float> ambientMap : register(t5);
StructuredBuffer<uint> surfaceMask : register(t6);
RWStructuredBuffer<uint> frame : register(u0);
RWStructuredBuffer<float> depths : register(u1);
RWStructuredBuffer<uint> resolved : register(u2);
RWStructuredBuffer<uint> surfaceFlags : register(u3);
cbuffer Settings : register(b0) {uint operation,clearColor,shadowOffset,quality,width,height,tileColumns,resolveScale;float3 enhancedKey;float ambientRadius;float3 enhancedFill;float ambientStrength;};
float4 unpack(uint v){return float4(v&255,(v>>8)&255,(v>>16)&255,v>>24);}
uint pack(float4 v){uint4 p=(uint4)clamp(v,0,255);return p.x|(p.y<<8)|(p.z<<16)|(p.w<<24);}
float edge(float2 a,float2 b,float2 p){return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);}
bool covered(float e,float2 a,float2 b,float area){
    if(area<0){e=-e;float2 swap=a;a=b;b=swap;}
    return e>0||(e==0&&(b.y<a.y||(b.y==a.y&&b.x>a.x)));
}
// Correct the reciprocal-multiply quotient at coverage/depth boundaries.
// Explicit mad retains the residual that a rounded multiply would discard.
float divide(float n,float d){float q=n/d;return mad(mad(-q,d,n),rcp(d),q);}
int coord(int p,int extent,bool wrap){return wrap?(p%extent+extent)%extent:clamp(p,0,extent-1);}
float4 fetch(Triangle t,int2 p){
    if(t.texInfo.w&64)return unpack(capture[p.y*width+p.x]);
    if(t.texInfo.w&2048){
        // Original 256x256 RGB scene copy, including byte quantization.
        float2 s=(float2(p)+.5)*float2(width,height)/256-.5;
        int2 i=max(int2(0,0),(int2)floor(s)),j=min(i+1,int2(width-1,height-1));float2 a=frac(s);
        float4 value=(unpack(capture[i.y*width+i.x])*(1-a.x)+unpack(capture[i.y*width+j.x])*a.x)*(1-a.y)
                    +(unpack(capture[j.y*width+i.x])*(1-a.x)+unpack(capture[j.y*width+j.x])*a.x)*a.y;
        return float4(trunc(value.xyz),255);
    }
    return unpack(texels[t.texInfo.x+p.y*t.texInfo.y+p.x]);
}
float4 sampleTexture(Triangle t,float2 uv){
    uint bits=t.texInfo.w;bool captured=(bits&1)!=0;
    bool wu=!captured&&(bits&4)!=0,wv=!captured&&(bits&8)!=0;
    if(wu)uv.x-=floor(uv.x);if(wv)uv.y-=floor(uv.y);
    int2 extent=(int2)t.texInfo.yz;
    bool sharpUpscale=!captured&&(bits&8192)!=0;
    if((bits&262144)||(!captured&&!(bits&2)&&!sharpUpscale)){
        float4 value=fetch(t,min(extent-1,(int2)(saturate(uv)*extent)));
        if(captured)value.w=255;return value;
    }
    float2 f=uv*extent-.5;
    if(!wu)f.x=clamp(f.x,0,extent.x-1);if(!wv)f.y=clamp(f.y,0,extent.y-1);
    int2 b=(int2)floor(f);float2 a=f-b;
    // Preserve a small stable texel center, then use a cubic transition. This
    // is sharper than bilinear without nearest-neighbour's hard square blocks.
    if(sharpUpscale){a=saturate((a-.125)*(4.0/3.0));a=a*a*(3-2*a);}
    int2 i=int2(coord(b.x,extent.x,wu),coord(b.y,extent.y,wv)),j=int2(coord(b.x+1,extent.x,wu),coord(b.y+1,extent.y,wv));
    float4 value=(fetch(t,i)*(1-a.x)+fetch(t,int2(j.x,i.y))*a.x)*(1-a.y)+(fetch(t,int2(i.x,j.y))*(1-a.x)+fetch(t,j)*a.x)*a.y;
    if(captured)value.w=255;return value;
}
float shadowTap(int2 p,float receiverDepth,uint size){
    // Root buffer SRVs have no bounds descriptor. Keep the address valid even
    // when the shader compiler evaluates both sides of a conditional select.
    int2 safe=clamp(p,0,int(size)-1);uint at=safe.y*size+safe.x;
    float value=(texels[shadowOffset+(at>>2)]>>((at&3)*8))&255,casterDepth=asfloat(texels[shadowOffset+(size*size+3)/4+at]);
    return all(p>=0)&&all(p<int(size))&&receiverDepth>=casterDepth?value:0;
}
float shadowSample(float3 p,float opacity,uint size){
    float2 f=(p.xy+1)*(size*.5)-.5;if(any(f<=-1)||any(f>=size))return 0;
    int2 i=(int2)floor(f);float2 a=f-i;
    return ((shadowTap(i,p.z,size)*(1-a.x)+shadowTap(i+int2(1,0),p.z,size)*a.x)*(1-a.y)
        +(shadowTap(i+int2(0,1),p.z,size)*(1-a.x)+shadowTap(i+1,p.z,size)*a.x)*a.y)*opacity;
}
float3 blendValue(uint selector,float3 source,float3 destination){return selector==0?source:selector==1?destination:0;}
float4 aaTexel(int2 p){p=clamp(p,int2(0,0),int2(width-1,height-1));return unpack(capture[p.y*width+p.x]);}
float4 aaSample(float2 p){
    int2 i=(int2)floor(p);precise float2 a=p-i;
    precise float4 result=(aaTexel(i)*(1-a.x)+aaTexel(i+int2(1,0))*a.x)*(1-a.y)
        +(aaTexel(i+int2(0,1))*(1-a.x)+aaTexel(i+1)*a.x)*a.y;return result;
}
float aaLuma(float4 p){precise float value=p.x*.299+p.y*.587+p.z*.114;return value;}
uint antiAliasedPixel(uint2 pixel){
    uint index=pixel.y*width+pixel.x,original=capture[index];
    int2 p=(int2)pixel.xy;float4 m=unpack(original),nw=aaTexel(p-1),ne=aaTexel(p+int2(1,-1)),sw=aaTexel(p+int2(-1,1)),se=aaTexel(p+1);
    if(min(m.w,min(min(nw.w,ne.w),min(sw.w,se.w)))<254)return original;
    precise float lm=aaLuma(m),lnw=aaLuma(nw),lne=aaLuma(ne),lsw=aaLuma(sw),lse=aaLuma(se);
    float low=min(lm,min(min(lnw,lne),min(lsw,lse))),high=max(lm,max(max(lnw,lne),max(lsw,lse)));
    if(high-low<max(8,high*.125))return original;
    precise float dx=-((lnw+lne)-(lsw+lse)),dy=(lnw+lsw)-(lne+lse);
    precise float reduce=max((lnw+lne+lsw+lse)*(.25*.125),255.0/128),inverse=divide(1,min(abs(dx),abs(dy))+reduce);
    dx=clamp(dx*inverse,-8,8);dy=clamp(dy*inverse,-8,8);
    precise float4 a=(aaSample(p-float2(dx,dy)/6)+aaSample(p+float2(dx,dy)/6))*.5;
    precise float4 b=a*.5+(aaSample(p-float2(dx,dy)*.5)+aaSample(p+float2(dx,dy)*.5))*.25;
    float lb=aaLuma(b);precise float3 value=clamp(lb<low||lb>high?a.xyz:b.xyz,0,255),whole=floor(value);
    // std::lround rounds half away from zero; keep the source alpha byte.
    uint3 rgb=(uint3)(whole+step(.5,value-whole));return (original&0xff000000)|rgb.x|(rgb.y<<8)|(rgb.z<<16);
}
[numthreads(16,16,1)]
void antiAlias(uint3 pixel:SV_DispatchThreadID){if(pixel.x<width&&pixel.y<height)frame[pixel.y*width+pixel.x]=antiAliasedPixel(pixel.xy);}
int4 styleTexel(int2 p){p=clamp(p,int2(0,0),int2(width-1,height-1));uint v=capture[p.y*width+p.x];return int4(v&255,(v>>8)&255,(v>>16)&255,v>>24);}
int styleLuma(int3 p){return (77*p.x+150*p.y+29*p.z+128)/256;}
uint styledPixel(uint2 pixel,uint style){
    uint index=pixel.y*width+pixel.x,original=(quality&16)?antiAliasedPixel(pixel.xy):capture[index];
    int2 p=(int2)pixel.xy;int4 m=styleTexel(p),l=styleTexel(p+int2(-1,0)),r=styleTexel(p+int2(1,0)),u=styleTexel(p+int2(0,-1)),d=styleTexel(p+int2(0,1));
    if(min(m.w,min(min(l.w,r.w),min(u.w,d.w)))<254)return original;
    int low=min(styleLuma(m.xyz),min(min(styleLuma(l.xyz),styleLuma(r.xyz)),min(styleLuma(u.xyz),styleLuma(d.xyz))));
    int high=max(styleLuma(m.xyz),max(max(styleLuma(l.xyz),styleLuma(r.xyz)),max(styleLuma(u.xyz),styleLuma(d.xyz))));
    int weight=64-min(high-low,64),limit=style==1?6:style==2?10:14;
    int3 rgb=int3(original&255,(original>>8)&255,(original>>16)&255);
    int3 detail=(4*m.xyz-l.xyz-r.xyz-u.xyz-d.xyz)*int(style)*weight/2048;
    rgb=clamp(rgb+clamp(detail,-limit,limit),0,255);
    if(style>=2){int lift=style==2?2:3,contrast=style==2?8:20;rgb=clamp(rgb+lift*rgb*(255-rgb)/16384+contrast*rgb*(255-rgb)*(2*rgb-255)/4194304,0,255);}
    int gray=styleLuma(rgb),saturation=style==1?256:style==2?264:276;rgb=clamp(gray+(rgb-gray)*saturation/256,0,255);
    return (original&0xff000000)|uint(rgb.x)|(uint(rgb.y)<<8)|(uint(rgb.z)<<16);
}
[numthreads(16,16,1)]
void visualStyle(uint3 pixel:SV_DispatchThreadID){if(pixel.x<width&&pixel.y<height)frame[pixel.y*width+pixel.x]=styledPixel(pixel.xy,quality&3);}
bool ambientValid(int2 p){int2 safe=clamp(p,int2(0,0),int2(width-1,height-1));uint at=safe.y*width+safe.x;float q=sceneDepth[at];return all(p>=0)&&p.x<int(width)&&p.y<int(height)&&surfaceMask[at]!=0&&isfinite(q)&&q>0;}
float3 viewPoint(int2 p){p=clamp(p,int2(0,0),int2(width-1,height-1));float z=1/sceneDepth[p.y*width+p.x],focal=asfloat(resolveScale);return float3((p.x+.5-width*.5)*z/focal,(height*.5-p.y-.5)*z/focal,z);}
int2 ambientPixel(int2 p){return min(p*2+1,int2(width-1,height-1));}
[numthreads(16,16,1)]
void ambientLighting(uint3 pixel:SV_DispatchThreadID){
    uint aw=(width+1)/2,ah=(height+1)/2;if(pixel.x>=aw||pixel.y>=ah)return;uint index=pixel.y*aw+pixel.x;resolved[index]=asuint(1.0);
    int2 at=ambientPixel(pixel.xy);if(!ambientValid(at))return;float3 p=viewPoint(at),dx=0,dy=0;float bestX=1e30,bestY=1e30;
    [unroll]for(int sign=-1;sign<=1;sign+=2){if(ambientValid(at+int2(sign,0))){float3 q=viewPoint(at+int2(sign,0));float change=abs(q.z-p.z);if(change<bestX){bestX=change;dx=(q-p)*sign;}}if(ambientValid(at+int2(0,sign))){float3 q=viewPoint(at+int2(0,sign));float change=abs(q.z-p.z);if(change<bestY){bestY=change;dy=(q-p)*sign;}}}
    if(bestX>max(.15,p.z*.04)||bestY>max(.15,p.z*.04))return;float3 normal=cross(dx,dy);float magnitude=dot(normal,normal);if(!isfinite(magnitude)||magnitude<1e-12)return;normal*=rsqrt(magnitude);if(dot(normal,p)>0)normal=-normal;
    float scale=min(float(width)/640,float(height)/448),pixels=clamp(asfloat(resolveScale)*ambientRadius/p.z,2*scale,14*scale),occlusion=0;
    const float2 offsets[8]={float2(1,0),float2(-.5,0),float2(0,1),float2(0,-.5),float2(.7071,.7071),float2(-.35355,.35355),float2(.7071,-.7071),float2(-.35355,-.35355)};
    [unroll]for(uint i=0;i<8;++i){int2 q=(int2)floor(float2(at)+offsets[i]*pixels+.5);if(!ambientValid(q))continue;float3 delta=viewPoint(q)-p;float distance=dot(delta,delta);if(distance<1e-6||distance>ambientRadius*ambientRadius)continue;occlusion+=max(0,(dot(normal,delta)-.035)/sqrt(distance))*(1-distance/(ambientRadius*ambientRadius));}
    float light=.82+.32*max(0,dot(normal,enhancedKey))+.12*max(0,dot(normal,enhancedFill));resolved[index]=asuint(clamp(max(.62,1-occlusion*ambientStrength)*light,.65,1.2));
}
[numthreads(16,16,1)]
void enhancedScene(uint3 pixel:SV_DispatchThreadID){
    if(pixel.x>=width||pixel.y>=height)return;uint index=pixel.y*width+pixel.x;int2 p=pixel.xy;
    if(!ambientValid(p)){frame[index]=(quality&16)?antiAliasedPixel(pixel.xy):capture[index];return;}uint original=styledPixel(pixel.xy,2);int4 m=styleTexel(p),l=styleTexel(p+int2(-1,0)),r=styleTexel(p+int2(1,0)),u=styleTexel(p+int2(0,-1)),d=styleTexel(p+int2(0,1));if(min(m.w,min(min(l.w,r.w),min(u.w,d.w)))<254){frame[index]=original;return;}
    float z=1/sceneDepth[index];int2 base=(int2)floor((float2(p)-1)*.5);uint aw=(width+1)/2,ah=(height+1)/2;int2 first=ambientPixel(clamp(base,int2(0,0),int2(aw-1,ah-1))),last=ambientPixel(clamp(base+1,int2(0,0),int2(aw-1,ah-1)));float2 a=saturate(float2(p-first)/max(last-first,1));float sum=0,weights=0;
    [unroll]for(int v=0;v<2;++v)[unroll]for(int u=0;u<2;++u){int2 cell=clamp(base+int2(u,v),int2(0,0),int2(aw-1,ah-1)),at=ambientPixel(cell);if(!ambientValid(at))continue;float dz=abs(1/sceneDepth[at.y*width+at.x]-z);if(dz>max(.12,z*.025))continue;float weight=(u?a.x:1-a.x)*(v?a.y:1-a.y)/(1+dz/max(.1,z*.02));sum+=ambientMap[cell.y*aw+cell.x]*weight;weights+=weight;}
    float light=weights>1e-6?sum/weights:1;uint3 rgb=(uint3)floor(clamp(float3(original&255,(original>>8)&255,(original>>16)&255)*light,0,255)+.5);frame[index]=(original&0xff000000)|rgb.x|(rgb.y<<8)|(rgb.z<<16);
}
groupshared float2 coverageA[64],coverageB[64],coverageC[64];
groupshared float coverageArea[64];
groupshared uint coverageIndex[64];
groupshared uint coverageStart,coverageCount;
void resolvePixel(uint2 pixel){
    if(pixel.x>=640||pixel.y>=448)return;
    float4 sum=0;uint2 first=pixel*resolveScale;
    [loop]for(uint y=0;y<resolveScale;++y)[loop]for(uint x=0;x<resolveScale;++x)sum+=unpack(frame[(first.y+y)*width+first.x+x]);
    resolved[pixel.y*640+pixel.x]=pack(round(sum/(resolveScale*resolveScale)));
}
[numthreads(16,16,1)]
void main(uint3 pixel:SV_DispatchThreadID,uint3 tile:SV_GroupID,uint thread:SV_GroupIndex){
    if(operation==2){resolvePixel(pixel.xy);return;}
    bool valid=pixel.x<width&&pixel.y<height;
    uint index=pixel.y*width+pixel.x;
#ifdef ENHANCED_RASTER
    const bool enhanced=true;
#else
    const bool enhanced=false;
#endif
    if(operation==0){if(valid){frame[index]=clearColor;depths[index]=0;if(enhanced)surfaceFlags[index]=0;}return;}
    if(thread==0){uint cell=(tile.y*tileColumns+tile.x)*2;coverageStart=tiles[cell];coverageCount=tiles[cell+1];}
    GroupMemoryBarrierWithGroupSync();
    uint start=coverageStart,count=coverageCount;
    bool initialize=operation==3;
    if(count==0){if(initialize&&valid){frame[index]=clearColor;depths[index]=0;if(enhanced)surfaceFlags[index]=0;}return;}
    float4 outColor=0;float depth=0;
    if(initialize){outColor=unpack(clearColor);depth=0;}
    else if(valid){outColor=unpack(frame[index]);depth=depths[index];}
    uint eligible=0;if(enhanced&&!initialize&&valid)eligible=surfaceFlags[index];
    // Share only coverage inputs; fetch material/vertex data after coverage.
    // Edge threads still participate in both barriers for partial screen tiles.
    [loop]for(uint base=0;base<count;base+=64){
        uint length=min(64,count-base);
        if(thread<length){uint at=tiles[start+base+thread];Triangle t=triangles[at];coverageIndex[thread]=at;coverageA[thread]=t.a.screen.xy;coverageB[thread]=t.b.screen.xy;coverageC[thread]=t.c.screen.xy;coverageArea[thread]=t.parameters.y;}
        GroupMemoryBarrierWithGroupSync();
        [loop]for(uint n=0;n<length;++n){
        if(!valid)continue;
        float2 ca=coverageA[n],cb=coverageB[n],cc=coverageC[n];float area=coverageArea[n];
        float2 p=float2(pixel.xy)+.5;
        if(quality&&(!covered(edge(cb,cc,p),cb,cc,area)
          ||!covered(edge(cc,ca,p),cc,ca,area)
          ||!covered(edge(ca,cb,p),ca,cb,area)))continue;
        precise float w0=divide(edge(cb,cc,p),area);
        precise float w1=divide(edge(cc,ca,p),area);
        precise float w2=1-w0-w1;if(!quality&&(w0<0||w1<0||w2<0))continue;
        Triangle t=triangles[coverageIndex[n]];uint bits=t.texInfo.w;
        precise float z=t.a.screen.z*w0+t.b.screen.z*w1+t.c.screen.z*w2;
        if(bits&16){
            if((bits&64)?z<depth:z<=depth)continue;
        }
        float q=(bits&1)?1:z;
        float2 uv=(t.a.uv.xy*w0+t.b.uv.xy*w1+t.c.uv.xy*w2)/q;
        float4 sampleColor=sampleTexture(t,uv);
        float4 color=(t.a.color*w0+t.b.color*w1+t.c.color*w2)*(1/z);
        float alpha=(bits&64)?t.parameters.x:saturate(sampleColor.w/255*color.w);
        // Native near-opaque hair occlusion policy, not a proven PS2 rule.
        // Phase selection and depth agree; sampled RGBA still drives blending.
        float opaqueAlphaThreshold=(bits&(32768|65536))?0.995:0.999;
        if(((bits&32768)&&alpha<opaqueAlphaThreshold)||((bits&65536)&&alpha>=opaqueAlphaThreshold))continue;
        if(alpha<=0&&!(bits&1024))continue;
        float3 source=sampleColor.xyz*color.xyz;
        if(bits&128){float fog=(t.a.screen.w*w0+t.b.screen.w*w1+t.c.screen.w*w2)/256;
            // Add/subtract light contributions fade to zero; the receiving
            // surface already contains fog. Adding fog again reveals the card.
            float3 fogColor=(bits&(256|512))&&!(bits&1024)?float3(0,0,0):t.fog.xyz;
            source=trunc(fogColor+(clamp(source,0,255)-fogColor)*fog);}
        if(bits&4096){float3 world=(t.a.shadow.xyz*w0+t.b.shadow.xyz*w1+t.c.shadow.xyz*w2)*(1/z);source*=1-shadowSample(world,t.parameters.z,(uint)t.parameters.w);}
        float3 rgb;
        if(bits&1024){float factor=t.blend.z==0?alpha:outColor.w/255;rgb=(blendValue(t.blend.x,source,outColor.xyz)-blendValue(t.blend.y,source,outColor.xyz))*factor+blendValue(t.blend.w,source,outColor.xyz);}
        else rgb=((bits&512)?-source:source)*alpha+outColor.xyz*((bits&(256|512))?1:1-alpha);
        float a=(bits&1024)?255*alpha:255*alpha+outColor.w*(1-alpha);
        outColor=trunc(clamp(float4(rgb,a),0,255));
        if(enhanced)eligible=alpha>=.999&&(bits&16)&&(bits&32)&&!(bits&(1|256|512|1024|2048|131072))&&(!(bits&128)||(t.a.screen.w*w0+t.b.screen.w*w1+t.c.screen.w*w2)>=255.744);
        if((bits&32)||((bits&16384)&&alpha>=opaqueAlphaThreshold))depth=z;
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if(!valid)return;
    frame[index]=pack(outColor);depths[index]=depth;
    if(enhanced)surfaceFlags[index]=eligible;
}

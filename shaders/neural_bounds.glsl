// Thor final-output anti-ringing. A bounded reconstruction stays within the
// per-channel range of the source bilinear footprint. No change to alpha.
vec3 sourceBounds(vec3 color,sampler2D source,ivec2 at,ivec2 outputSize){
 ivec2 size=textureSize(source,0);
 // Exact half-pixel footprint, including an odd-sized image's center. Computing
 // floor(uv*size-.5) with approximate GPU reciprocals can choose the other cell
 // at an integer boundary and create a visible range discontinuity.
 ivec2 lo=((2*at+1)*size+outputSize)/(2*outputSize)-1;
 vec3 a=texelFetch(source,clamp(lo,ivec2(0),size-1),0).rgb;
 vec3 b=texelFetch(source,clamp(lo+ivec2(1,0),ivec2(0),size-1),0).rgb;
 vec3 c=texelFetch(source,clamp(lo+ivec2(0,1),ivec2(0),size-1),0).rgb;
 vec3 d=texelFetch(source,clamp(lo+ivec2(1,1),ivec2(0),size-1),0).rgb;
 return clamp(color,min(min(a,b),min(c,d)),max(max(a,b),max(c,d)));
}

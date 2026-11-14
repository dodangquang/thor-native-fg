// Thor final-output anti-ringing. A bounded reconstruction stays within the
// per-channel range of the source bilinear footprint. No change to alpha.
// ENHANCED: gradient-aware bounds expansion for better detail on edges
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
 
 vec3 minRGB = min(min(a,b),min(c,d));
 vec3 maxRGB = max(max(a,b),max(c,d));
 
 // Detect local gradient for adaptive bounds
 vec3 gradH = abs(b - a) + abs(d - c);
 vec3 gradV = abs(c - a) + abs(d - b);
 vec3 gradient = max(gradH, gradV);
 
 // On strong edges/gradients, expand bounds slightly to preserve detail
 // On flat regions, keep tight bounds to prevent ringing
 vec3 edgeFactor = clamp(gradient * 3.0, vec3(0.0), vec3(1.0));
 vec3 expansion = (maxRGB - minRGB) * edgeFactor * 0.15;
 
 minRGB -= expansion;
 maxRGB += expansion;
 
 // Perceptual clamping: preserve luma better than individual channels
 // This prevents color shifts while still controlling ringing
 float lumaIn = dot(color, vec3(0.299, 0.587, 0.114));
 float lumaMin = dot(minRGB, vec3(0.299, 0.587, 0.114));
 float lumaMax = dot(maxRGB, vec3(0.299, 0.587, 0.114));
 
 vec3 clamped = clamp(color, minRGB, maxRGB);
 
 // If luma is within bounds but color is clamped, try to preserve luma
 if (lumaIn >= lumaMin && lumaIn <= lumaMax) {
     float lumaClamped = dot(clamped, vec3(0.299, 0.587, 0.114));
     if (abs(lumaClamped - lumaIn) > 0.02) {
         // Adjust clamped color to restore original luma where possible
         vec3 restored = clamped + (color - clamped) * 0.5;
         clamped = clamp(restored, minRGB, maxRGB);
     }
 }
 
 return clamped;
}

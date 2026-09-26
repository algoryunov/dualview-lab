export const metalVertex = `void main(){gl_Position=vec4(position.xy,0.,1.);}`
export const metalFragment = `
precision highp float;
uniform vec2 resolution;
uniform vec4 viewport;
uniform vec4 intrinsics;
uniform float lens[8];
uniform vec3 origin;
uniform vec3 eye;
uniform vec3 target;
uniform mat3 cameraToWorld;
uniform vec4 blobs[5];
uniform vec4 flow[9];
uniform vec4 fingers[42];
uniform int handCount;
uniform float time;
uniform float opacity;
uniform float agitation;
uniform float fusion;
uniform float finish;
uniform bool stage;
uniform float stretched;
uniform bool occlusion;
uniform float quality;
const float UNIT=.075;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float smin(float a,float b,float k){float h=clamp(.5+.5*(b-a)/k,0.,1.);return mix(b,a,h)-k*h*(1.-h);}
float field(vec3 p){
 float d=100.;vec4 a=flow[0];
 for(int i=1;i<=8;i++){
  vec4 b=flow[i];vec3 ab=b.xyz-a.xyz;
  float u=clamp(dot(p-a.xyz,ab)/max(dot(ab,ab),.00001),0.,1.);
  float segment=length(p-a.xyz-u*ab)-mix(a.w,b.w,u);
  d=smin(d,segment,.12);
  a=b;
 }
 // Broad, low-amplitude capillary waves, not a noisy surface.
 float wave=sin(p.x*3.7+time*.8)*sin(p.y*4.1-time*.65)*sin(p.z*3.3+time*.5);
 return d+wave*(.012+agitation*.018);
}
vec3 normalAt(vec3 p){
 vec2 e=vec2(.0015,0.);
 return normalize(vec3(field(p+e.xyy)-field(p-e.xyy),field(p+e.yxy)-field(p-e.yxy),field(p+e.yyx)-field(p-e.yyx)));
}
float ambientOcclusion(vec3 p,vec3 n){
 float occ=0.;float weight=1.;
 for(int i=1;i<=4;i++){float h=float(i)*.07;occ+=(h-field(p+n*h))*weight;weight*=.55;}
 return clamp(1.-occ*2.3,.3,1.);
}
float boxLight(vec3 r,vec3 direction,vec3 up,vec2 size){
 vec3 right=normalize(cross(direction,up));up=normalize(cross(right,direction));
 float plane=dot(r,direction);
 vec2 q=vec2(dot(r,right),dot(r,up))/max(plane,.001);
 vec2 edge=abs(q)-size;
 return (1.-smoothstep(-.025,.055,max(edge.x,edge.y)))*step(.0,plane);
}
vec3 room(vec3 r){
 vec3 c=mix(vec3(.008,.013,.023),vec3(.13,.17,.21),smoothstep(-.6,.9,-r.y));
 float key=boxLight(r,normalize(vec3(-.65,-.6,-.7)),vec3(0.,1.,0.),vec2(.22,.95));
 float strip=boxLight(r,normalize(vec3(.8,-.1,.2)),vec3(0.,1.,0.),vec2(.08,.85));
 float top=boxLight(r,normalize(vec3(.1,-.95,.15)),vec3(0.,0.,1.),vec2(.7,.2));
 float rim=boxLight(r,normalize(vec3(-.5,.2,.8)),vec3(0.,1.,0.),vec2(.11,.9));
 c+=vec3(1.,.94,.84)*key*4.8;
 c+=vec3(.3,.7,1.)*strip*3.;
 c+=vec3(.92,.97,1.)*top*3.;
 c+=vec3(1.,.39,.12)*rim*2.4;
 // A horizon gives the polished surface a stable sense of curvature.
 c+=vec3(.2,.24,.3)*exp(-pow((r.y-.18)*17.,2.));
 return c;
}
vec3 film(vec3 x){return clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.);}
vec3 surface(vec3 p,vec3 n,vec3 rd){
 vec3 reflected=reflect(rd,n);
 vec3 env=room(reflected);
 vec3 tangent=normalize(cross(n,abs(n.y)<.9?vec3(0.,1.,0.):vec3(1.,0.,0.)));
 env=env*.76+.12*room(normalize(reflected+tangent*.035))+.12*room(normalize(reflected-tangent*.035));
 float facing=max(0.,dot(n,-rd));
 vec3 base=vec3(.91,.94,.98);
 if(finish>1.5) base=vec3(1.,.64,.25);
 else if(finish>.5) base=mix(vec3(.22,.68,.92),vec3(.85,.3,.64),.5+.5*cos(facing*6.+p.y*.6));
 vec3 fresnel=base+(vec3(1.)-base)*pow(1.-facing,5.);
 float ao=ambientOcclusion(p,n);
 return env*fresnel*ao+base*.028;
}
// Closest approach between a perspective camera ray and a tracked finger bone.
float boneMask(vec3 ro,vec3 rd,vec3 a,vec3 b,float radius,float hitDistance){
 vec3 u=b-a,w=ro-a;float c=dot(u,u),du=dot(rd,u),dw=dot(rd,w),uw=dot(u,w);
 float den=max(c-du*du,.00001);
 float s=clamp((uw-du*dw)/den,0.,1.);
 float t=max(0.,dot(a+s*u-ro,rd));
 float distance=length(ro+rd*t-(a+s*u));
 return (1.-smoothstep(radius*.83,radius*1.12,distance))*step(t,hitDistance-.025);
}
float handMask(vec3 ro,vec3 rd,float hitDistance){
 float mask=0.;
 for(int h=0;h<2;h++){
  if(h>=handCount)break;
  int offset=h*21;
  for(int finger=0;finger<5;finger++){
   int base=offset+1+finger*4;
   for(int joint=0;joint<3;joint++){
    vec4 a=fingers[base+joint],b=fingers[base+joint+1];
    mask=max(mask,boneMask(ro,rd,a.xyz,b.xyz,a.w,hitDistance));
   }
   mask=max(mask,boneMask(ro,rd,fingers[offset].xyz,fingers[base].xyz,.11,hitDistance));
  }
  mask=max(mask,boneMask(ro,rd,fingers[offset+5].xyz,fingers[offset+17].xyz,.19,hitDistance));
 }
 return mask;
}
vec2 undistort(vec2 xy){
 vec2 p=xy;
 for(int i=0;i<6;i++){
  float r2=dot(p,p),r4=r2*r2,r6=r4*r2;
  float radial=(1.+lens[0]*r2+lens[1]*r4+lens[4]*r6)/(1.+lens[5]*r2+lens[6]*r4+lens[7]*r6);
  vec2 tangential=vec2(2.*lens[2]*p.x*p.y+lens[3]*(r2+2.*p.x*p.x),lens[2]*(r2+2.*p.y*p.y)+2.*lens[3]*p.x*p.y);
  p=(xy-tangential)/max(radial,.01);
 }
 return p;
}
void main(){
 vec2 screen=vec2(gl_FragCoord.x,resolution.y-gl_FragCoord.y);
 vec3 ro,rd;vec3 background=vec3(0.);
 if(stage){
  vec2 composition=vec2(resolution.x/resolution.y>1.5?.56:.5,.5);
  vec2 uv=(screen-resolution*composition)/resolution.y;
  vec3 forward=normalize(target-eye),right=normalize(cross(vec3(0.,1.,0.),forward)),up=cross(forward,right);
  ro=eye;rd=normalize(forward+right*uv.x*.92+up*uv.y*.92);
  float radial=length(uv*vec2(.65,1.));
  background=mix(vec3(.045,.065,.092),vec3(.006,.012,.024),smoothstep(.0,.85,radial));
  // Ground receives a broad, soft contact shadow under the floating sculpture.
  float floorT=(1.6-ro.y)/rd.y;
  if(floorT>0.){
   vec3 floorPoint=ro+rd*floorT;
   float shadow=exp(-dot(floorPoint.xz,floorPoint.xz)*.48)*.72;
   background*=1.-shadow;
   background+=vec3(.013,.024,.032)*exp(-abs(floorPoint.z)*.25);
  }
 }else{
  if(opacity<.002)discard;
  vec2 pixel=(screen-viewport.xy)/viewport.zw;
  if(any(lessThan(pixel,vec2(0.)))||any(greaterThan(pixel,vec2(1.))))discard;
  vec2 xy=(pixel-intrinsics.zw)/intrinsics.xy;
  rd=normalize(cameraToWorld*vec3(undistort(xy),1.));ro=cameraToWorld*(-origin/UNIT);
 }
 // Conservative bounding sphere covers all merged droplets.
 float bound=1.;for(int i=0;i<9;i++)bound=max(bound,length(flow[i].xyz)+flow[i].w+.3);
 float b=dot(ro,rd),c=dot(ro,ro)-bound*bound,disc=b*b-c;
 float travel=0.;float limit=0.;bool hit=false;
 if(disc>0.){
  travel=max(0.,-b-sqrt(disc));limit=-b+sqrt(disc);
  for(int i=0;i<100;i++){
   if(float(i)>quality)break;
   float d=field(ro+rd*travel);
   if(d<.0018){hit=true;break;}
   travel+=max(.001,d*.8);if(travel>limit)break;
  }
 }
 vec3 color=background;float alpha=stage?1.:0.;
 if(hit){
  vec3 p=ro+rd*travel,n=normalAt(p);
  color=surface(p,n,rd);
  if(!stage && occlusion)alpha=opacity*(1.-handMask(ro,rd,travel));else alpha=stage?1.:opacity;
  if(stage)color=mix(background,color,opacity);
 }
 color=film(color*1.25);
 color=pow(color,vec3(1./2.2));
 if(stage)color+=(hash(gl_FragCoord.xy)-.5)/255.;
 gl_FragColor=vec4(color,alpha);
}
`

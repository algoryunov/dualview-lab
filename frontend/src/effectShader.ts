export const effectFragment = `
precision highp float;
uniform vec2 resolution;
uniform vec2 center;
uniform float radius;
uniform float angle;
uniform float time;
uniform float opacity;
uniform float spread;
uniform int mode;
mat2 rot(float a) { return mat2(cos(a),-sin(a),sin(a),cos(a)); }
void main() {
 if(opacity<.001) discard;
 vec2 p=(gl_FragCoord.xy-center)/max(radius,1.);
 vec3 color=vec3(0.);float alpha=0.;
 if(mode==0) {
  float r=length(p);float a=atan(p.y,p.x);
  float shell=exp(-pow((r-.78)*18.,2.));
  float aura=exp(-r*r*2.1);
  float filaments=0.;
  for(int i=0;i<6;i++) {
   float f=float(i);
   vec2 q=rot(f*.52+time*.15)*p;
   float ring=length(vec2(q.x,q.y*(1.4+.5*sin(time*.4+f))));
   filaments+=.012/(.012+abs(ring-(.63+.07*sin(a*5.+time*1.8+f))));
  }
  color=vec3(.03,.25,.8)*aura + vec3(.12,.85,1.)*shell*.8+vec3(.25,.5,1.)*filaments*.38;
  color+=vec3(.8,.95,1.)*exp(-r*r*22.)*.65;
  alpha=clamp(aura*.45+shell*.65+filaments*.22,0.,.95);
 }
 color=vec3(1.)-exp(-color*1.3);
 gl_FragColor=vec4(color,alpha*opacity);
}
`

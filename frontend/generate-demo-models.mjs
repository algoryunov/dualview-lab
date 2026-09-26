import * as T from 'three';
import { GLTFExporter } from 'three/examples/jsm/exporters/GLTFExporter.js';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import { RoundedBoxGeometry } from 'three/examples/jsm/geometries/RoundedBoxGeometry.js';
import { writeFile } from 'node:fs/promises';

// The exporter uses the browser FileReader API even for texture-free assets.
globalThis.FileReader = class {
  readAsArrayBuffer(blob) { blob.arrayBuffer().then(value => { this.result = value; this.onloadend?.(); }); }
  readAsDataURL(blob) { blob.arrayBuffer().then(value => { this.result = `data:${blob.type};base64,${Buffer.from(value).toString('base64')}`; this.onloadend?.(); }); }
};
const mat = (color, roughness = .65, metalness = 0) => new T.MeshStandardMaterial({ color, roughness, metalness });
function add(g, name, geometry, material, position, scale) {
  const m = new T.Mesh(geometry, material); m.name = name;
  m.position.set(...position); if (scale) m.scale.set(...scale); g.add(m); return m;
}
const box = (g,n,s,m,p,r=.04) => add(g,n,new RoundedBoxGeometry(...s,2,r),m,p);
const ball = (g,n,s,m,p) => add(g,n,new T.SphereGeometry(1,16,10),m,p,s);
function rod(g,n,a,b,r,m) {
  const start=new T.Vector3(...a),end=new T.Vector3(...b),d=end.clone().sub(start);
  const mesh=add(g,n,new T.CylinderGeometry(r,r,d.length(),10),m,start.add(end).multiplyScalar(.5).toArray());
  mesh.quaternion.setFromUnitVectors(new T.Vector3(0,1,0),d.normalize()); return mesh;
}
function car() {
  const g=new T.Group(); const paint=mat('#19b6b1',.3,.2), dark=mat('#172736'),glass=mat('#91d7e7',.22,.3),cream=mat('#fff1cc'),red=mat('#f45b63'),silver=mat('#d7e1e5',.25,.65);
  box(g,'Turquoise body',[1.65,.38,.8],paint,[0,.44,0],.12);
  box(g,'Cabin',[.91,.43,.70],paint,[-.12,.80,0],.13);
  for(const z of [-.354,.354]) {
    box(g,'Side rear window',[.32,.25,.025],glass,[-.34,.82,z]);
    box(g,'Side front window',[.32,.25,.025],glass,[.05,.82,z]);
    box(g,'Door handle',[.12,.027,.026],silver,[-.10,.59,z*1.14]);
  }
  box(g,'Windshield',[.025,.25,.52],glass,[.338,.82,0]);
  box(g,'Rear window',[.025,.24,.50],glass,[-.577,.82,0]);
  for(const x of [-.53,.54]) for(const z of [-.42,.42]) {
    const wheel=add(g,'Rubber tire',new T.CylinderGeometry(.225,.225,.13,20),dark,[x,.25,z]); wheel.rotation.x=Math.PI/2;
    const hub=add(g,'Wheel hub',new T.CylinderGeometry(.12,.12,.138,12),silver,[x,.25,z]); hub.rotation.x=Math.PI/2;
  }
  for(const z of [-.255,.255]) {
    box(g,'Headlight',[.035,.12,.19],cream,[.823,.48,z]);
    box(g,'Tail light',[.035,.10,.17],red,[-.823,.47,z]);
  }
  box(g,'Front bumper',[.06,.09,.59],silver,[.83,.32,0]);
  return g;
}
function bed() {
  const g=new T.Group(),wood=mat('#a66a48'),edge=mat('#cf976b'),white=mat('#fff5e9'),blue=mat('#527bd8'),light=mat('#88aff2');
  box(g,'Bed frame',[1.28,.20,2.0],wood,[0,.29,0]);
  for(const x of [-.49,.49]) for(const z of [-.81,.81]) box(g,'Wooden leg',[.14,.26,.14],wood,[x,.13,z]);
  box(g,'Headboard',[1.34,.88,.13],edge,[0,.63,-.94],.07);
  box(g,'Headboard inset',[1.13,.42,.035],wood,[0,.82,-.86],.06);
  box(g,'Cream mattress',[1.22,.23,1.88],white,[0,.49,0],.10);
  box(g,'Blue duvet',[1.25,.15,1.23],blue,[0,.64,.32],.075);
  box(g,'Folded duvet edge',[1.25,.065,.19],light,[0,.735,-.20]);
  for(const x of [-.31,.31]) box(g,'Soft pillow',[.53,.14,.40],white,[x,.66,-.62],.065);
  for(const x of [-.43,0,.43]) box(g,'Duvet seam',[.012,.008,1.0],light,[x,.719,.37],.003);
  return g;
}
function flower() {
  const g=new T.Group(),terra=mat('#d77652'),rim=mat('#eea276'),soil=mat('#513628'),green=mat('#398a4c'),leaf=mat('#71b74e'),pink=mat('#ee6b9d'),gold=mat('#ffce56');
  add(g,'Terracotta pot',new T.CylinderGeometry(.31,.22,.43,24),terra,[0,.215,0]);
  add(g,'Pot rim',new T.CylinderGeometry(.335,.335,.085,24),rim,[0,.405,0]);
  add(g,'Soil',new T.CylinderGeometry(.294,.294,.014,24),soil,[0,.452,0]);
  rod(g,'Stem',[0,.45,0],[.035,1.35,0],.025,green);
  rod(g,'Left leaf stalk',[.01,.76,0],[-.19,.91,0],.014,green);
  rod(g,'Right leaf stalk',[.02,.96,0],[.22,1.06,0],.014,green);
  const l=ball(g,'Left leaf',[.22,.075,.095],leaf,[-.20,.93,0]); l.rotation.z=-.5;
  const r=ball(g,'Right leaf',[.22,.075,.095],leaf,[.24,1.08,0]); r.rotation.z=.45;
  // Flower faces +Z, with fully volumetric petals and a visible green calyx behind.
  ball(g,'Calyx',[.14,.14,.07],green,[.035,1.36,-.07]);
  for(let i=0;i<8;i++) {
    const a=i*Math.PI/4;
    const p=ball(g,`Pink petal ${i+1}`,[.115,.23,.075],pink,[.035+Math.sin(a)*.22,1.36+Math.cos(a)*.22,0]); p.rotation.z=-a;
  }
  ball(g,'Golden flower center',[.135,.135,.10],gold,[.035,1.36,.075]);
  for(let i=0;i<9;i++) { const a=i*2.399;const d=.025*Math.sqrt(i);ball(g,'Pollen',[.013,.013,.01],rim,[.035+Math.cos(a)*d,1.36+Math.sin(a)*d,.166]); }
  return g;
}
for(const [name,build] of [['car',car],['bed',bed],['flower',flower]]) {
  const g=build();g.name=name;
  // Shared convention: Y up, centered pivot, longest dimension = 1 unit.
  g.updateMatrixWorld(true);const bounds=new T.Box3().setFromObject(g), center=bounds.getCenter(new T.Vector3()),size=bounds.getSize(new T.Vector3());
  const root=new T.Group();root.name=name;g.position.sub(center);root.add(g);root.scale.setScalar(1/Math.max(size.x,size.y,size.z));
  const binary=await new GLTFExporter().parseAsync(root,{binary:true});
  const parsed=await new GLTFLoader().parseAsync(binary,'');let triangles=0,meshes=0;
  parsed.scene.traverse(o=>{if(o.isMesh){meshes++;triangles+=(o.geometry.index?.count??o.geometry.attributes.position.count)/3;}});
  const measured=new T.Box3().setFromObject(parsed.scene).getSize(new T.Vector3());
  if(Math.abs(Math.max(...measured.toArray())-1)>1e-5)throw Error('Invalid normalized size');
  await writeFile(new URL(`./public/models/${name}.glb`,import.meta.url),Buffer.from(binary));
  console.log(`${name}: ${binary.byteLength} bytes, ${meshes} meshes, ${triangles} triangles; round-trip OK`);
}

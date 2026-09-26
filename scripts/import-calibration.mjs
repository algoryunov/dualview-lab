import fs from 'node:fs'
import path from 'node:path'
// Convert local JSON profiles to the native OpenCV YAML schema, preserving originals.
const [laptopPath, phonePath, stereoPath, output = 'data/calibration/native.yml'] = process.argv.slice(2)
if (!laptopPath || !phonePath || !stereoPath) throw new Error('Usage: node scripts/import-calibration.mjs LAPTOP.json PHONE.json STEREO.json [OUTPUT.yml]')
if (fs.existsSync(output)) throw new Error(`Refusing to overwrite ${output}`)
const profiles = [laptopPath, phonePath].map(file => JSON.parse(fs.readFileSync(file, 'utf8')))
const stereo = JSON.parse(fs.readFileSync(stereoPath, 'utf8'))
function matrix(name, rows, cols, data) {
  if (data.length !== rows * cols || !data.every(Number.isFinite)) throw new Error(`Invalid matrix: ${name}`)
  return `${name}: !!opencv-matrix\n   rows: ${rows}\n   cols: ${cols}\n   dt: d\n   data: [ ${data.join(', ')} ]\n`
}
let result = '%YAML:1.0\n---\n'
profiles.forEach((p, i) => {
  if (!p.usable_for_metric_calibration || !(p.width > 0 && p.height > 0)) throw new Error('Invalid intrinsic profile')
  result += `width${i}: ${p.width}\nheight${i}: ${p.height}\n`
  result += matrix(`K${i}`, 3, 3, p.camera_matrix.flat())
  result += matrix(`D${i}`, 1, p.distortion_coefficients.length, p.distortion_coefficients)
})
result += `calibrated: ${stereo.status === 'calibrated' ? 1 : 0}\nhealth: "${stereo.health === 'healthy' ? 'healthy' : 'degraded'}"\n`
result += matrix('R', 3, 3, stereo.rotation_phone_from_laptop.flat())
result += matrix('T', 3, 1, stereo.translation_phone_from_laptop_m)
const error = stereo.median_stereo_reprojection_error_px ?? stereo.median_reprojection_error_px
if (!Number.isFinite(stereo.baseline_cm) || !Number.isFinite(error)) throw new Error('Invalid calibration quality metrics')
result += `baseline_cm: ${stereo.baseline_cm}\nerror_px: ${error}\n`
fs.mkdirSync(path.dirname(output), { recursive: true })
fs.writeFileSync(output, result, { flag: 'wx' })
console.log(`Imported local calibration into ${output}; originals preserved.`)

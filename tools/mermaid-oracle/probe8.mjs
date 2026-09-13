import { line, curveBasis } from 'd3-shape';
const pts = [[100, 80], [100, 105], [100, 130]];
const d = line().curve(curveBasis)(pts);
console.log('spline:', d);
console.log('golden: M100,80L100,84.167C100,88.333,100,96.667,100,102C100,107.333,100,109.667,100,110.833L100,112');

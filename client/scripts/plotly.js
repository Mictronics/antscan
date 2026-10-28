/* global Plotly, liveDataStatus, liveDataStatusEl, liveDataStatusAz */
/* exported init_plot, append, draw_plot */

/* Measurement data per frequency (MHz as string): { azimut: [], elevation: [], gain: [] } */
let m_data = {};
/* Measurement frequencies in input order, index = dropdown selection */
let freqList = [];

function init_plot(testFrequency, len) {
    m_data = {};
    freqList = [];
    for (let i = 0; i < len; i++) {
        freqList.push(`${testFrequency[i]}`);
        m_data[`${testFrequency[i]}`] = {
            azimut: [],
            elevation: [],
            gain: []
        };
    }
    draw_plot();
}

function deg2rad(degree) {
    return degree * (Math.PI / 180);
}

function append(azimut, elevation, frequency, gain) {
    const data = m_data[`${frequency}`];
    if (!data) {
        return;
    }
    data.azimut.push(azimut);
    data.elevation.push(elevation);
    data.gain.push(parseFloat(gain.toFixed(1)));
    draw_plot();
}

function polarLayout(title, sector) {
    const polar = { angularaxis: { direction: 'clockwise', dtick: 30 } };
    if (sector) {
        polar.sector = sector;
    }
    return {
        autosize: false,
        width: 500,
        height: 500,
        title,
        font: { family: 'Arial, sans-serif', size: 12, color: '#000' },
        showlegend: false,
        polar
    };
}

function polarTrace(radius, theta, thetaName) {
    return [{
        r: radius,
        theta,
        mode: 'lines',
        line: { color: 'darkviolet', shape: 'spline', smoothing: 1.3 },
        type: 'scatterpolar',
        hovertemplate: `Gain: %{r:.1f}<br>${thetaName}: %{theta}`
    }];
}

/**
 * Gain over the angle of one axis while the other axis is fixed at fixedValue.
 */
function slice(data, fixedKey, fixedValue, varyKey) {
    const radius = [];
    const theta = [];
    for (let i = 0; i < data.gain.length; i++) {
        if (data[fixedKey][i] === fixedValue) {
            radius.push(data.gain[i]);
            theta.push(data[varyKey][i]);
        }
    }
    return [radius, theta];
}

/**
 * Surface on the elevation x azimut grid. Radius is the gain shifted so the weakest
 * point sits at the origin (gain is in dB and can be negative). Cells not measured
 * yet are null.
 */
function surface3d(data) {
    const elIndex = new Map();
    const azIndex = new Map();
    data.elevation.forEach((el) => elIndex.has(el) || elIndex.set(el, elIndex.size));
    data.azimut.forEach((az) => azIndex.has(az) || azIndex.set(az, azIndex.size));
    const azList = [...azIndex.keys()];
    /* A full circle: close the seam by repeating the first column */
    const closed = azList.length > 2 && (azList[0] - azList[azList.length - 1] + 360) % 360 === (azList[1] - azList[0] + 360) % 360;
    const cols = azList.length + (closed ? 1 : 0);
    const grid = () => Array.from({ length: elIndex.size }, () => new Array(cols).fill(null));
    const x = grid();
    const y = grid();
    const z = grid();
    const r = grid();
    const minGain = Math.min(...data.gain);
    data.gain.forEach((gain, i) => {
        const row = elIndex.get(data.elevation[i]);
        const el = deg2rad(data.elevation[i]);
        const az = deg2rad(data.azimut[i]);
        const radius = gain - minGain;
        const cells = [azIndex.get(data.azimut[i])];
        if (closed && cells[0] === 0) {
            cells.push(cols - 1);
        }
        cells.forEach((col) => {
            x[row][col] = radius * Math.cos(el) * Math.cos(az);
            y[row][col] = radius * Math.cos(el) * Math.sin(az);
            z[row][col] = radius * Math.sin(el);
            r[row][col] = radius;
        });
    });
    return [{ type: 'surface', x, y, z, surfacecolor: r, colorscale: 'Viridis', showscale: false }];
}

function draw_plot() {
    const frequency = freqList[liveDataStatus];
    const data = m_data[frequency];
    if (!data) {
        Plotly.react('dataField', polarTrace([], [], 'Azimut'), polarLayout('Measurement'));
        Plotly.react('dataField2', polarTrace([], [], 'Elevation'), polarLayout('Measurement', [0, 180]));
        Plotly.react('dataField3', [{ type: 'scatter3d', x: [], y: [], z: [] }], { title: 'Measurement' });
        return;
    }
    const [r1, theta1] = slice(data, 'elevation', liveDataStatusEl, 'azimut');
    const [r2, theta2] = slice(data, 'azimut', liveDataStatusAz, 'elevation');
    Plotly.react('dataField', polarTrace(r1, theta1, 'Azimut'),
        polarLayout(`Measurement data ${frequency}MHz, ${liveDataStatusEl}° elevation`));
    Plotly.react('dataField2', polarTrace(r2, theta2, 'Elevation'),
        polarLayout(`Measurement data ${frequency}MHz, ${liveDataStatusAz}° azimut`, [0, 180]));
    Plotly.react('dataField3', data.gain.length ? surface3d(data) : [{ type: 'scatter3d', x: [], y: [], z: [] }],
        { title: `Measurement data ${frequency}MHz`, scene: { aspectmode: 'data' } });
}

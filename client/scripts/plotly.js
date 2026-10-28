let m_data = {};
let r_data = {};
/* Measurement frequencies in input order, index = dropdown selection */
let freqList = [];

function init_plot(testFrequency, len) {
    m_data = {};
    r_data = {};
    freqList = [];
    for (let i = 0; i < len; i++) {
        freqList.push(`${testFrequency[i]}`);
        m_data[`${testFrequency[i]}`] = {
            azimut: [],
            elevation: [],
            gain: []
        }
        r_data[`${testFrequency[i]}`] = {
            x: [],
            y: [],
            z: []
        }
    }
    var data = [
        {
            r: [],
            theta: [],
            mode: 'lines',
            line: { color: 'darkviolet' },
            type: 'scatterpolar'
        }
    ]

    var layout1 = {
        autosize: false,
        width: 500,
        height: 500,
        title: 'Measurement',
        font: {
            family: 'Arial, sans-serif;',
            size: 12,
            color: '#000'
        },
        showlegend: false,
        polar: {
            angularaxis: {
                direction: "clockwise",
                dtick: 30
            }
        }
    };

    var layout2 = {
        autosize: false,
        width: 500,
        height: 500,
        title: 'Measurement',
        font: {
            family: 'Arial, sans-serif;',
            size: 12,
            color: '#000'
        },
        showlegend: false,
        polar: {
            sector: [0, 180],
            angularaxis: {
                direction: "clockwise",
                dtick: 30
            }
        }
    };

    Plotly.newPlot('dataField', data, layout1);
    Plotly.newPlot('dataField2', data, layout2);

    var data3d = [
        {
            opacity: 0.8,
            color: 'rgb(300,100,200)',
            type: 'mesh3d',
            x: [],
            y: [],
            z: [],
        }
    ];

    Plotly.newPlot('dataField3', data3d);
}

function deg2rad(degree) {
    var rad = degree * (Math.PI / 180);
    return rad;
}

function append(azimut, elevation, frequency, gain) {
    if (!m_data[`${frequency}`]) {
        return;
    }
    gain = parseFloat(gain.toFixed(1));
    console.info(`rounded gain: ${gain}`);

    m_data[`${frequency}`].azimut.push(azimut);
    m_data[`${frequency}`].elevation.push(elevation);
    m_data[`${frequency}`].gain.push(gain);

    var xn = gain * Math.cos(deg2rad(elevation));
    var yn = 0;
    var zn = gain * Math.sin(deg2rad(elevation));

    //Rotation Matrix X
    var x = xn;
    var y = Math.cos(deg2rad(azimut)) * yn - Math.sin(deg2rad(azimut)) * zn;
    var z = Math.sin(deg2rad(azimut)) * yn + Math.cos(deg2rad(azimut)) * zn;

    //remove last item
    r_data[`${frequency}`].x.splice(-1, 1);
    r_data[`${frequency}`].y.splice(-1, 1);
    r_data[`${frequency}`].z.splice(-1, 1);

    //append new values
    r_data[`${frequency}`].x.push(x);
    r_data[`${frequency}`].y.push(y);
    r_data[`${frequency}`].z.push(z);

    //add first item at the end
    r_data[`${frequency}`].x.push(r_data[`${frequency}`].x[0]);
    r_data[`${frequency}`].y.push(r_data[`${frequency}`].y[0]);
    r_data[`${frequency}`].z.push(r_data[`${frequency}`].z[0]);

    draw_plot();
}

function draw_plot() {
    var frequency = freqList[liveDataStatus];
    var predata = [
        {
            r: [],
            theta: [],
            mode: 'lines',
            line: {
                color: 'darkviolet',
                shape: 'spline',
                smoothing: 1.3
            },
            type: 'scatterpolar',
            hovertemplate: 'Gain: %{r:.1f}<br>Azimut: %{theta}'
        }
    ]
    var prelayout1 = {
        autosize: false,
        width: 500,
        height: 500,
        title: `Measurement data ${frequency}MHz`,
        font: {
            family: 'Arial, sans-serif;',
            size: 12,
            color: '#000'
        },
        showlegend: false,
        polar: {
            angularaxis: {
                direction: "clockwise",
                dtick: 30
            }
        }
    };

    var prelayout2 = {
        autosize: false,
        width: 500,
        height: 500,
        title: `Measurement data ${frequency}MHz`,
        font: {
            family: 'Arial, sans-serif;',
            size: 12,
            color: '#000'
        },
        showlegend: false,
        polar: {
            sector: [0, 180],
            angularaxis: {
                direction: "clockwise",
                dtick: 30
            }
        }
    };

    var predata3d = [
        {
            opacity: 0.8,
            color: 'rgb(300,100,200)',
            type: 'mesh3d',
            x: [],
            y: [],
            z: [],
        }
    ];

    Plotly.react('dataField', predata, prelayout1);
    Plotly.react('dataField2', predata, prelayout2);
    Plotly.react('dataField3', predata3d);

    var data1 = built_data1();
    var layout1 = built_layout1();
    var data2 = built_data2();
    var layout2 = built_layout2();
    var data3d = built_data3d();

    Plotly.react('dataField', data1, layout1);
    Plotly.react('dataField2', data2, layout2);
    Plotly.react('dataField3', data3d);
}

function built_data1() {
    var frequency = freqList[liveDataStatus];
    var radius = [];
    var theta2 = [];

    for (let i = 0; i < m_data[frequency].gain.length; i++) {
        if (m_data[frequency].elevation[i] == liveDataStatusEl) {
            radius.push(m_data[frequency].gain[i]);
            theta2.push(m_data[frequency].azimut[i]);
        }
    }

    var data = [
        {
            r: radius,
            theta: theta2,
            mode: 'lines',
            line: {
                color: 'darkviolet',
                shape: 'spline',
                smoothing: 1.3
            },
            type: 'scatterpolar',
            hovertemplate: 'Gain: %{r:.1f}<br>Azimut: %{theta}'
        }
    ]

    return data;
}

function built_data2() {
    var frequency = freqList[liveDataStatus];
    var radius = [];
    var theta2 = [];

    for (let i = 0; i < m_data[frequency].gain.length; i++) {
        if (m_data[frequency].azimut[i] == liveDataStatusAz) {
            radius.push(m_data[frequency].gain[i]);
            theta2.push(m_data[frequency].elevation[i]);
        }
    }

    var data = [
        {
            r: radius,
            theta: theta2,
            mode: 'lines',
            line: {
                color: 'darkviolet',
                shape: 'spline',
                smoothing: 1.3
            },
            type: 'scatterpolar',
            hovertemplate: 'Gain: %{r:.1f}<br>Elevation: %{theta}'
        }
    ]

    return data;
}

function built_layout1() {
    var frequency = freqList[liveDataStatus];
    var layout = {
        autosize: false,
        width: 500,
        height: 500,
        title: `Measurement data ${frequency}MHz, ${liveDataStatusEl}° elevation`,
        font: {
            family: 'Arial, sans-serif;',
            size: 12,
            color: '#000'
        },
        showlegend: false,
        polar: {
            angularaxis: {
                direction: "clockwise",
                dtick: 30
            }
        }
    };

    return layout;
}

function built_layout2() {
    var frequency = freqList[liveDataStatus];
    var layout = {
        autosize: false,
        width: 500,
        height: 500,
        title: `Measurement data ${frequency}MHz, ${liveDataStatusAz}° azimut`,
        font: {
            family: 'Arial, sans-serif;',
            size: 12,
            color: '#000'
        },
        showlegend: false,
        polar: {
            sector: [0, 180],
            angularaxis: {
                direction: "clockwise",
                dtick: 30
            }
        }
    };

    return layout;
}

function built_data3d() {
    var frequency = freqList[liveDataStatus];
    var data = [
        {
            alphahull: 0,
            opacity: 0.8,
            color: 'rgb(300,100,200)',
            type: 'mesh3d',
            x: r_data[`${frequency}`].x,
            y: r_data[`${frequency}`].y,
            z: r_data[`${frequency}`].z,
        }
    ];

    return data;
}

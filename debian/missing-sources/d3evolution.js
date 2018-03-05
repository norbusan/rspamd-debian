/*!
 * D3Evolution 1.1.0 (https://github.com/moisseev/D3Evolution)
 * Copyright (c) 2016-2017, Alexander Moisseev, BSD 2-Clause
 */

function D3Evolution(id, options) {
    "use strict";

    var opts = $.extend(true, {
        title: "",
        width: 800,
        height: 400,
        margin: {top: 80, right: 60, bottom: 40, left: 60},
        yAxisLabel: "",

        type: "line", // area|line
        yScale: "lin", // lin|log

        duration: 1250,
        interpolate: "curveLinear",

        //  convert: "percentage",

        legend: {
            buttonRadius: 7,
            space: 130,

            entries: [
                //  ,
                //  ,
                //  {label: "Greylisted", color: "#436EEE"},
                //  {label: "Clean",      color: "#66cc00"},
            ]
        }
    }, options);

    const curves = {
        "curveLinear":          d3.curveLinear,
        "curveStep":            d3.curveStep,
        "curveStepBefore":      d3.curveStepBefore,
        "curveStepAfter":       d3.curveStepAfter,
        "curveMonotoneX":       d3.curveMonotoneX,
        "curveBasis":           d3.curveBasis,
        "curveBasisOpen":       d3.curveBasisOpen,
        "curveBundle":          d3.curveBundle,
        "curveCardinal":        d3.curveCardinal,
        "curveCardinalOpen":    d3.curveCardinalOpen,
        "curveCatmullRom":      d3.curveCatmullRom,
        "curveCatmullRomOpen":  d3.curveCatmullRomOpen,
        "curveNatural":         d3.curveNatural,
    };

    var data;
    var srcData;
    var legendX;

    var width = opts.width - opts.margin.left - opts.margin.right;
    var height = opts.height - opts.margin.top - opts.margin.bottom;

    var xScale = d3.scaleTime().range([0, width]);

    var yScale;
    var yAxisScale;

    const setYScale = function () {
        if (opts.yScale === "log") {
            yScale = d3.scaleLog().clamp(true).range([height, 0]);
            yAxisScale = d3.scaleLog().range([height - 30, 0]);
        } else {
            yScale = d3.scaleLinear().range([height, 0]);
            yAxisScale = yScale.copy();
        }
    };

    setYScale();

    var xAxis = d3.axisBottom().scale(xScale);
    var yAxis = d3.axisLeft().scale(yAxisScale).ticks(5);

    var xAxisGrid = d3.axisBottom().tickFormat("").scale(xScale)
        .tickSize(-height, 0);
    var yAxisGrid = d3.axisLeft().tickFormat("").scale(yAxisScale)
        .tickSize(-width, 0);

    var yScaleBoolean = d3.scaleQuantize().range([height, 0]);
    var areaNull = d3.area()
        .x(function (d) { return xScale(d.x); })
        .y0(function (d) { return height; })
        .y1(function (d) { return yScaleBoolean(d.y == null); })
        .curve(d3.curveStep);

    var line = d3.line()
        .defined(function(d) { return d.y != null; })
        .x(function (d) { return xScale(d.x); })
        .y(function (d) { return yScale(d.y); })
        .curve(curves[opts.interpolate]);

    var area = d3.area()
        .defined(function(d) { return d.y != null; })
        .x(function (d) { return xScale(d.x); })
        .y0(function (d) { return yScale(d.y0); })
        .y1(function (d) { return yScale(d.y0 + d.y); })
        .curve(curves[opts.interpolate]);

    var d3v3LayoutStack = function (data) {
        data.reduce(function (res, curr) {
            curr.map(function (d, i) {
                d.y0 = (res.length ? res[i].y + res[i].y0 : 0);
            });
            return curr;
        }, []);
    };

    var stack = function () {
        var yExtents;

        if (opts.type === "area") {
            d3v3LayoutStack(data);
            yExtents = (opts.yScale === "log")
                ? d3.extent(d3.merge(data), function (d) { return ((d.y0 + d.y) === 0) ? null : d.y0 + d.y; })
                : d3.extent(d3.merge(data), function (d) { return d.y0 + d.y; });
        } else {
            yExtents = (opts.yScale === "log")
                ? d3.extent(d3.merge(data), function (d) { return (d.y === 0) ? null : d.y; })
                : d3.extent(d3.merge(data), function (d) { return d.y; });
        }

        if (opts.yScale === "log") {
            if (yExtents[0] === undefined) {
                yExtents = [.0095, .0105];
            } else if (yExtents[0] === yExtents[1]) {
                yExtents[0] *= .9;
            }
            yAxisScale.domain([yExtents[0], yExtents[1]]);
            var y0 = yAxisScale.invert(height);
            yScale.domain([y0, yExtents[1]]);
        } else {
            yScale.domain([(yExtents[0] > 0) ? 0 : yExtents[0], yExtents[1]]);
            yAxisScale.domain(yScale.domain());
        }

        /**
         * Hide overlapping tick labels on logarithmic Y-axis.
         * @param {number} d - Tick value.
         * @param {object} p - Previous unhidden label.
         * @param {number} p.y - Y-position of the tick.
         * @param {string} f - Tick label format.
         * @returns {string} Tick label format or empty string.
         */
        function logFormat(d, p, f) {
            // Minimal interval of labeled ticks
            const minInterval = 15;
            // The nearest power of 10.
            const pow10 = Math.pow(10, Math.round(Math.log(d) / Math.LN10));

            if (
                // Never hide labels of power of 10 tick marks
                !(Math.abs(pow10 - d) < 1e-6) && (
                    // Hide if the next power of 10 tick mark is too close
                    (Math.abs(yScale(pow10) - yScale(d)) < minInterval) ||
                    // Hide if previous label is too close
                    ((p.y - yScale(d)) < minInterval)
                )
            ) {
                return "";
            }

            p.y = yScale(d);
            return f(d);
        }

        if (opts.convert === "percentage") {
            var prevUnhidLabel = {y: height}; // Previous unhidden tick label
            const percentFormat = d3.format(".0%");
            y0Axis.tickFormat(percentFormat);
            yAxis.tickFormat((opts.yScale === "log")
                ? function (d) { return logFormat(d, prevUnhidLabel, percentFormat) ;}
                : percentFormat);
        } else {
            y0Axis.tickFormat(null);
            yAxis.tickFormat(null);
        }

        /**
         * In some cases when extent values are to close (e.g. 0.00011 and 0.00019
         * on log scale), there are no ticks generated. Possible D3 bug.
         * We should set at least labels for extents if there are no any ticks.
         */
        yAxis.tickValues(!yAxisScale.ticks().length ? [yExtents[0], yExtents[1]] : null);

        const t = d3.transition()
            .duration(opts.duration);

        g.select(".y.grid").transition(t).call(yAxisGrid.scale(yAxisScale));
        g.select(".y.axis").transition(t).call(yAxis.scale(yAxisScale));
        g.select(".y-zero.axis").call(y0Axis);
    };

    var colorScale = d3.scaleOrdinal(d3.schemeCategory10);

    var pathColor = function (i) {
        return (opts.legend.entries[i] !== undefined &&
                opts.legend.entries[i].color !== undefined) ?
            opts.legend.entries[i].color :
            colorScale(i);
    };

    var pathLabel = function (i) {
        return (opts.legend.entries[i] !== undefined &&
                opts.legend.entries[i].label !== undefined) ?
            opts.legend.entries[i].label :
            "path_" + i;
    };

    var convert2Percentage = function (a) {
        var total = a.reduce(function (res, curr) {
            return curr.map(function (d, i) { return d.y + (res[i] ? res[i] : 0); });
        }, []);

        var dataPercentage = $.extend(true, [], a);

        dataPercentage.forEach(function (s) {
            s.forEach(function (d, i) { if (total[i]) {d.y /= total[i];} });
        });

        return dataPercentage;
    }

    var yPreprocess = function () {
        if (opts.convert === "percentage") {
            yAxisLabel.transition().duration(opts.duration).style("opacity", 0);
            data = convert2Percentage(srcData);
        } else {
            yAxisLabel.transition().duration(opts.duration).style("opacity", 1);
            data = srcData;
//                data = $.extend(true, [], srcData)
        }
        stack();
    };

    /**
     * Substitute real zeroes with values mapped to zero position on the graph.
     */
    const substY0 = function () {
        if (opts.yScale === "log") {
            const y0 = yScale.invert(height);
            data.forEach(function (s) {
                s.forEach(function (d, i) { return d.y == 0 ? d.y : y0; });
            });
        }
    };

    var svg = d3.select("#" + id).append("svg")
        .classed('d3evolution', true)
        .attr("width", opts.width)
        .attr("height", opts.height);

    var legend = svg.append("g").attr("class", "legend");

    var g = svg.append("g")
        .attr("width", width)
        .attr("height", height)
        .attr("transform", "translate(" + opts.margin.left + ", " + opts.margin.top + ")");

    g.append("g")
        .attr("class", "x grid")
        .attr("transform", "translate(0," + height + ")")
        .call(xAxisGrid);

    g.append("g")
        .attr("class", "y grid")
        .attr("transform", "translate(0,0)")
        .call(yAxisGrid);

    g.append("g")
        .attr("class", "x axis")
        .attr("transform", "translate(0," + height + ")")
        .call(xAxis);

    g.append("g")
        .attr("class", "y axis")
        .attr("transform", "translate(0,0)")
        .call(yAxis);

    // Zero tick mark for log scale
    var y0Scale = d3.scaleOrdinal().domain([0]).range([height]);
    var y0Axis = d3.axisLeft().scale(y0Scale);
    g.append("g")
        .attr("class", "y-zero axis")
        .call(y0Axis);

    var yAxisLabel = g.append("text")
        .attr("class", "y label")
        .attr("x", 20 - opts.margin.left)
        .attr("y", -20)
        .style("opacity", (opts.convert === "percentage") ? 0 : 1)
        .text(opts.yAxisLabel);

    var title = svg.append("svg:text")
        .attr("x", (opts.width / 2))
        .attr("y", (opts.margin.top / 3))
        .attr("text-anchor", "middle");

    title.append("tspan")
        .attr("class", "chart-title")
        .text(opts.title + " ");

    title.timeRange = title.append("tspan");

    this.data = function (a) {
        srcData = $.extend(true, [], a);

        var opacity = [];

        legendX = opts.width - opts.margin.right - opts.legend.space * srcData.length;

        // Convert time in seconds to milliseconds
        srcData.forEach(function (s) {
            s.forEach(function (d) { d.x *= 1000; });
        });

        var xExtents = d3.extent(d3.merge(srcData), function (d) { return d.x; });
        xScale.domain([xExtents[0], xExtents[1]]);

        const iso = function (a) {
            return d3.timeFormat("%Y-%m-%d %H:%M:%S")(new Date(a));
        };
        title.timeRange
            .text("[ " + iso(xExtents[0]) + " / " + iso(xExtents[1]) + " ]");

        var pathNull = g.selectAll("path.path-null").data(srcData);

        pathNull.enter()
            .append("path")
            .attr("class", "path-null");

        g.selectAll("path.path-null")
            .transition().duration(opts.duration / 2)
            .style("opacity", 0)
            .on("end", function () {
                g.selectAll("path.path-null")
                    .attr("d", areaNull)
                    .transition().duration(opts.duration / 2)
                    .style("opacity", 1);
            });

        pathNull.exit()
            .remove();

        yPreprocess();
        substY0();

        var path = g.selectAll("path.path").data(data);

        path.enter()
            .append("path")
            .merge(path)
            .attr("class", "path")
            .attr("id", function (d, i) { return "path_" + i; })
            .on("click",     function (d, i) { onClick(i); })
            .on("mouseover", function (d, i) { highlight(i); })
            .on("mouseout",  function (d, i) { highlight(i, false); });

        path.exit()
            .remove();

        path = g.selectAll("path.path");

        if (opts.type === "area") {
            path
                .style("fill", function (d, i) { return pathColor(i); })
                .style("stroke", "none")
                .style("fill-opacity", function (d, i) { return opacity[i]; });
        } else {
            path
                .style("fill", "none")
                .style("stroke", function (d, i) { return pathColor(i); })
                .style("opacity", function (d, i) { return opacity[i]; });
        }

        path
            .transition().duration(opts.duration)
            .attr("d", (opts.type === "area") ? area : line);

        const t = d3.transition()
            .duration(opts.duration);

        g.select(".x.grid").transition(t).call(xAxisGrid.scale(xScale));
        g.select(".x.axis").transition(t).call(xAxis.scale(xScale));

        var onClick = function (i) {
            opacity[i] = (opacity[i] != 0) ? 0 : 1;

            d3.select("#circle_" + i)
                .transition().duration(opts.duration)
                .style("fill-opacity", opacity[i] + 0.2);

            d3.select("#path_" + i)
                .transition().duration(opts.duration)
                .style("opacity", opacity[i]);
        };

        /**
         * Highlight selected path and legend circle.
         * @param {number} s - Selected path index.
         * @param {boolean} [h] - If false, restore previous state.
         */
        const highlight = function (s, h) {
            d3.select("#circle_" + s)
                .attr("r", opts.legend.buttonRadius * (h === false ? 1 : 1.3));

            const op = function (i) {
                if (h === false)
                    return opacity[i];
                return (i === s) ? 1 : (opacity[i] == 0) ? 0 : 0.4;
            };

            g.selectAll("path.path")
                .style("opacity",      function (d, i) { return op(i); })
                .style("fill-opacity", function (d, i) { return op(i); });
        };

        var buttons = legend.selectAll("circle").data(data);

        buttons.enter().append("circle")
            .attr("id", function (d, i) { return "circle_" + i; })
            .attr("cy", opts.margin.top * 2 / 3)
            .attr("r", opts.legend.buttonRadius)
            .style("fill",   function (d, i) { return pathColor(i); })
            .style("stroke", function (d, i) { return pathColor(i); })
            .style("fill-opacity", function (d, i) { return opacity[i] + 0.2; })
            .on("click",     function (d, i) { onClick(i); })
            .on("mouseover", function (d, i) { highlight(i); })
            .on("mouseout",  function (d, i) { highlight(i, false); });

        buttons.exit()
            .remove();

        legend.selectAll("circle")
            .transition().duration(opts.duration)
            .attr("cx", function (d, i) { return legendX + opts.legend.space * i; });

        var labels = legend.selectAll("text").data(data);

        labels.enter()
            .append("text")
            .attr("y", opts.margin.top * 2 / 3)
            .attr("dy", "0.3em")
            .text(function (d, i) { return pathLabel(i); })
            .on("click",     function (d, i) { onClick(i); })
            .on("mouseover", function (d, i) { highlight(i); })
            .on("mouseout",  function (d, i) { highlight(i, false); });

        labels.exit()
            .remove();

        legend.selectAll("text")
            .transition().duration(opts.duration)
            .attr("x", function (d, i) {
                return legendX + opts.legend.space * i + 2 * opts.legend.buttonRadius;
            });

        return this;
    };

    this.legend = function (a) {
        $.extend(true, opts.legend, a);

        legend.selectAll("circle")
            .transition().duration(opts.duration)
            .attr("cx", function (d, i) { return legendX + opts.legend.space * i; })
            .attr("r", opts.legend.buttonRadius)
            .style("fill",   function (d, i) { return pathColor(i); })
            .style("stroke", function (d, i) { return pathColor(i); });

        legend.selectAll("text")
            .text(function (d, i) { return pathLabel(i); })
            .transition().duration(opts.duration)
            .attr("x", function (d, i) {
                return legendX + opts.legend.space * i + 2 * opts.legend.buttonRadius;
            });

        g.selectAll("path.path")
            .transition().duration(opts.duration)
            .style("fill",   (opts.type === "area") ? function (d, i) { return pathColor(i); } : "none")
            .style("stroke", (opts.type !== "area") ? function (d, i) { return pathColor(i); } : "none");

        return this;
    };

    this.convert = function (a) {
        opts.convert = a;

        yPreprocess();

        g.selectAll("path.path")
            .data(data)
            .transition().duration(opts.duration)
            .attr("d", (opts.type === "area") ? area : line);

        return this;
    };

    this.interpolate = function (a) {
        opts.interpolate = a;

        area.curve(curves[opts.interpolate]);
        line.curve(curves[opts.interpolate]);

        g.selectAll("path.path")
            .attr("d", (opts.type === "area") ? area : line);

        return this;
    };

    this.type = function (a) {
        opts.type = a;

        stack();

        g.selectAll("path.path")
            .style("stroke", (opts.type !== "area") ? function (d, i) { return pathColor(i); } : "none")
            .style("fill",   (opts.type === "area") ? function (d, i) { return pathColor(i); } : "none")
            .transition().duration(opts.duration)
            .attr("d", (opts.type === "area") ? area : line);

        return this;
    };

    this.yAxisLabel = function (a) {
        opts.yAxisLabel = a;
        yAxisLabel
            .transition().duration(opts.duration / 2)
            .style("opacity", 0)
            .on("end", function () {
                yAxisLabel
                    .text(opts.yAxisLabel)
                    .transition().duration(opts.duration / 2)
                    .style("opacity", 1);
            });

        return this;
    };

    this.yScale = function (a) {
        opts.yScale = a;

        setYScale();
        substY0();
        stack();

        g.selectAll("path.path")
            .transition().duration(opts.duration)
            .attr("d", (opts.type === "area") ? area : line);

        return this;
    };

    this.destroy = function () {
        d3.select("svg").remove();
    };
}

/* zan-charts 0.1.6 browser bundle: 源码为纯 ESM 无外部依赖，按依赖序包 IIFE 串联，公开符号挂 window.ZanCharts */
(function(exports){
var ctx = {};
(function(){
/* ---- types.js ---- */

})();
(function(){
/* ---- theme.js ---- */
const defaultMargins = {
    top: 28,
    right: 16,
    bottom: 32,
    left: 44
};
const defaultTheme = {
    background: '#ffffff',
    border: '#e5e7eb',
    text: '#111827',
    axis: '#6b7280',
    grid: '#f3f4f6',
    palette: ['#3b82f6', '#10b981', '#f59e0b', '#8b5cf6', '#ef4444', '#14b8a6']
};

ctx["defaultTheme"] = defaultTheme;
ctx["defaultMargins"] = defaultMargins;
})();
(function(){
/* ---- tooltip.js ---- */
const round = (value) => {
    return Math.round(value * 100) / 100;
};
const esc = (value) => {
    return value
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&apos;');
};
const getAxis = (model, axisIndex) => {
    return model.yAxes.find(item => item.index === axisIndex) || model.yAxes[0];
};
const formatCartesianValue = (axis, series, point) => {
    if (series.labelFormatter) {
        return series.labelFormatter(point.value, {
            category: point.category,
            index: point.categoryIndex,
            field: series.field,
            label: series.label
        });
    }
    if (axis.formatter) {
        return axis.formatter(point.value);
    }
    return String(round(point.value));
};
const normalizeContent = (content, isHtml) => {
    if (!content) {
        return null;
    }
    return {
        content,
        isHtml
    };
};
const buildCartesianPointContext = (model, series, point) => {
    const axis = getAxis(model, series.yAxisIndex);
    return {
        chartType: 'cartesian',
        trigger: 'item',
        category: point.category,
        categoryIndex: point.categoryIndex,
        field: series.field,
        label: series.label,
        color: series.color,
        value: point.value,
        formattedValue: formatCartesianValue(axis, series, point),
        axisIndex: series.yAxisIndex
    };
};
const buildPieContext = (model, field) => {
    if (!model.pie) {
        return null;
    }
    const slice = model.pie.slices.find(item => item.field === field);
    if (!slice) {
        return null;
    }
    const formattedValue = model.pie.valueFormatter
        ? model.pie.valueFormatter(slice.value)
        : String(round(slice.value));
    const percentage = slice.ratio * 100;
    return {
        chartType: 'pie',
        trigger: 'item',
        category: slice.label,
        categoryIndex: 0,
        field: slice.field,
        label: slice.label,
        color: slice.color,
        value: slice.value,
        formattedValue,
        axisIndex: 0,
        ratio: slice.ratio,
        percentage,
        percentageLabel: `${Math.round(percentage)}%`
    };
};
const buildDefaultSharedHtml = (context) => {
    const rows = context.points
        .map(point => [
        '<div style="display:flex;align-items:center;justify-content:space-between;gap:12px">',
        `<span style="display:flex;align-items:center;gap:6px"><span style="width:8px;height:8px;border-radius:999px;background:${point.color};display:inline-block"></span>${esc(point.label)}</span>`,
        `<strong style="font-weight:600">${esc(point.formattedValue)}</strong>`,
        '</div>'
    ].join(''))
        .join('');
    if (!rows) {
        return '';
    }
    return [
        `<div style="font-weight:600;margin-bottom:6px">${esc(context.category)}</div>`,
        rows
    ].join('');
};
const buildDefaultItemText = (context) => {
    if (context.chartType === 'pie') {
        return `${context.label}: ${context.formattedValue} (${context.percentageLabel})`;
    }
    return `${context.label} / ${context.category}: ${context.formattedValue}`;
};
const resolveSharedTooltipContent = (model, absoluteIndex) => {
    if (model.type === 'pie') {
        return null;
    }
    const localIndex = absoluteIndex - model.visibleStartIndex;
    const category = model.categories[localIndex];
    if (!category) {
        return null;
    }
    const points = model.series
        .filter(item => item.visible)
        .map((series) => {
        const point = series.points.find(item => item.categoryIndex === absoluteIndex);
        return point ? buildCartesianPointContext(model, series, point) : null;
    })
        .filter((item) => Boolean(item));
    if (points.length === 0) {
        return null;
    }
    const context = {
        chartType: 'cartesian',
        trigger: 'shared',
        category,
        categoryIndex: absoluteIndex,
        points
    };
    if (model.tooltip.formatter) {
        return normalizeContent(model.tooltip.formatter(context), true);
    }
    return normalizeContent(buildDefaultSharedHtml(context), true);
};
const resolveItemTooltipContent = (model, selector) => {
    if (model.type === 'pie') {
        const context = buildPieContext(model, selector.field);
        if (!context) {
            return null;
        }
        if (model.tooltip.formatter) {
            return normalizeContent(model.tooltip.formatter(context), true);
        }
        return normalizeContent(buildDefaultItemText(context), false);
    }
    const series = model.series.find(item => item.field === selector.field && item.visible);
    const point = series?.points.find(item => item.categoryIndex === selector.categoryIndex);
    if (!series || !point) {
        return null;
    }
    const context = buildCartesianPointContext(model, series, point);
    if (model.tooltip.formatter) {
        return normalizeContent(model.tooltip.formatter(context), true);
    }
    return normalizeContent(buildDefaultItemText(context), false);
};

})();
(function(){
/* ---- model.js ---- */
var defaultMargins = ctx["defaultMargins"];
var defaultTheme = ctx["defaultTheme"];
const toFiniteNumber = (value) => {
    if (typeof value === 'number' && Number.isFinite(value)) {
        return value;
    }
    if (typeof value === 'string') {
        const parsed = Number(value.trim());
        if (Number.isFinite(parsed)) {
            return parsed;
        }
    }
    return 0;
};
const resolveOrientation = (input) => {
    return input === 'horizontal' ? 'horizontal' : 'vertical';
};
const resolveMargins = (input) => {
    return {
        ...defaultMargins,
        ...(input || {})
    };
};
const resolveTheme = (input) => {
    return {
        ...defaultTheme,
        ...(input || {}),
        palette: input?.palette && input.palette.length > 0 ? input.palette : defaultTheme.palette
    };
};
const resolveSize = (options) => {
    const width = Math.max(240, Math.floor(options.width || 640));
    const resolvedHeight = options.height === undefined ? 320 : toFiniteNumber(options.height);
    const height = Math.max(1, Math.floor(resolvedHeight));
    return { width, height };
};
const resolveCategories = (options) => {
    return options.data.map((row, index) => {
        const raw = row?.[options.categoryField];
        if (raw === undefined || raw === null || String(raw) === '') {
            return `项目 ${index + 1}`;
        }
        return String(raw);
    });
};
const resolveSeriesType = (chartType, seriesType) => {
    if (seriesType === 'bar' || seriesType === 'line' || seriesType === 'area') {
        return seriesType;
    }
    if (chartType === 'line' || chartType === 'area') {
        return chartType;
    }
    return 'bar';
};
const resolveLegendRows = (labels, availableWidth) => {
    if (labels.length === 0) {
        return 0;
    }
    let rows = 1;
    let rowWidth = 0;
    labels.forEach((label) => {
        const itemWidth = Math.max(72, label.length * 8 + 34);
        if (rowWidth > 0 && rowWidth + itemWidth > availableWidth) {
            rows += 1;
            rowWidth = itemWidth;
            return;
        }
        rowWidth += itemWidth;
    });
    return rows;
};
const normalizeReferenceLines = (options) => {
    return (options || []).map((item, index) => ({
        value: toFiniteNumber(item.value),
        label: item.label,
        color: item.color || (index % 2 === 0 ? '#f59e0b' : '#ef4444'),
        lineDash: item.lineDash || '6 4',
        axisIndex: item.yAxisIndex === 1 ? 1 : 0
    }));
};
const normalizeReferenceAreas = (options) => {
    return (options || []).map((item, index) => ({
        start: toFiniteNumber(item.start),
        end: toFiniteNumber(item.end),
        label: item.label,
        color: item.color || (index % 2 === 0 ? '#93c5fd' : '#86efac'),
        opacity: item.opacity ?? 0.18,
        axisIndex: item.yAxisIndex === 1 ? 1 : 0
    }));
};
const normalizeEmptyState = (input) => {
    return {
        text: input?.text || '暂无数据',
        subtext: input?.subtext
    };
};
const clampPieInnerRadiusRatio = (value) => {
    if (typeof value !== 'number' || !Number.isFinite(value)) {
        return 0.58;
    }
    return Math.max(0.2, Math.min(0.85, value));
};
const resolveAxisConfig = (options, needsSecondaryAxis) => {
    const axes = [options.yAxes?.[0] || {}];
    if (needsSecondaryAxis) {
        axes.push(options.yAxes?.[1] || {});
    }
    if (!axes[0].formatter && options.yAxisFormatter) {
        axes[0].formatter = options.yAxisFormatter;
    }
    return axes;
};
const resolveAxisDomain = (series, referenceLines, referenceAreas, axisOptions, axisIndex) => {
    const values = [];
    series
        .filter(item => item.visible && item.yAxisIndex === axisIndex)
        .forEach((item) => {
        item.points.forEach((point) => {
            values.push(point.stackStart, point.stackEnd);
        });
    });
    referenceLines
        .filter(item => item.axisIndex === axisIndex)
        .forEach(item => values.push(item.value));
    referenceAreas
        .filter(item => item.axisIndex === axisIndex)
        .forEach(item => values.push(item.start, item.end));
    let minValue = values.length > 0 ? Math.min(...values) : 0;
    let maxValue = values.length > 0 ? Math.max(...values) : 1;
    if (typeof axisOptions?.min === 'number') {
        minValue = axisOptions.min;
    }
    else {
        minValue = Math.min(0, minValue);
    }
    if (typeof axisOptions?.max === 'number') {
        maxValue = axisOptions.max;
    }
    else {
        maxValue = Math.max(0, maxValue);
    }
    if (minValue === maxValue) {
        maxValue = minValue + 1;
    }
    return {
        minValue,
        maxValue
    };
};
const chartHeaderGap = 10;
const titleLineHeight = 18;
const legendRowHeight = 18;
const resolveHeaderLayout = (topMargin, options, legendRows) => {
    const hasTitle = Boolean(options.title);
    const hasLegend = options.showLegend !== false && legendRows > 0;
    const titleHeight = hasTitle ? titleLineHeight : 0;
    const legendHeight = hasLegend ? legendRows * legendRowHeight : 0;
    let cursorY = topMargin;
    let titleY;
    let legendY;
    if (hasTitle) {
        titleY = cursorY + titleLineHeight;
        cursorY += titleHeight;
    }
    if (hasLegend) {
        if (hasTitle) {
            cursorY += chartHeaderGap;
        }
        legendY = cursorY + legendRowHeight / 2;
        cursorY += legendHeight;
    }
    return {
        top: topMargin,
        height: cursorY - topMargin,
        titleHeight,
        titleY,
        legendHeight,
        legendY,
        legendRowHeight
    };
};
const resolveLayoutMargins = (baseMargins, headerLayout, needsSecondaryAxis, orientation) => {
    const margins = { ...baseMargins };
    margins.top += headerLayout.height;
    if (needsSecondaryAxis) {
        margins.right = Math.max(margins.right, 56);
    }
    if (orientation === 'horizontal') {
        margins.left = Math.max(margins.left, 72);
        margins.bottom = Math.max(margins.bottom, 40);
    }
    return margins;
};
const buildPieModel = (options, series, width, height, margins) => {
    const donut = Boolean(options.pie?.donut);
    const innerRadiusRatio = donut ? clampPieInnerRadiusRatio(options.pie?.innerRadiusRatio) : 0;
    const total = series.reduce((sum, item) => {
        const value = Math.max(0, item.points[0]?.value || 0);
        return sum + value;
    }, 0);
    const centerX = margins.left + (width - margins.left - margins.right) / 2;
    const centerY = margins.top + (height - margins.top - margins.bottom) / 2;
    const outerRadius = Math.max(24, Math.min(width - margins.left - margins.right, height - margins.top - margins.bottom) / 2 - 10);
    const innerRadius = outerRadius * innerRadiusRatio;
    let cursor = -Math.PI / 2;
    const slices = series
        .filter(item => item.visible)
        .map((item) => {
        const value = Math.max(0, item.points[0]?.value || 0);
        const ratio = total <= 0 ? 0 : value / total;
        const startAngle = cursor;
        const endAngle = startAngle + Math.PI * 2 * ratio;
        cursor = endAngle;
        return {
            field: item.field,
            label: item.label,
            color: item.color,
            value,
            ratio,
            startAngle,
            endAngle
        };
    })
        .filter(item => item.value > 0);
    return {
        donut,
        innerRadiusRatio,
        showSliceLabels: options.pie?.showSliceLabels !== false,
        centerX,
        centerY,
        outerRadius,
        innerRadius,
        total,
        totalLabel: options.pie?.totalLabel,
        valueFormatter: options.pie?.valueFormatter,
        slices
    };
};
const buildChartModel = (options) => {
    const type = options.type || 'bar';
    const orientation = resolveOrientation(options.orientation);
    const theme = resolveTheme(options.theme);
    const { width, height } = resolveSize(options);
    const allCategories = resolveCategories(options);
    const totalCategoryCount = allCategories.length;
    const maxIndex = Math.max(0, totalCategoryCount - 1);
    const zoomStart = Math.max(0, Math.min(options.zoomWindow?.startIndex ?? 0, maxIndex));
    const zoomEnd = totalCategoryCount === 0
        ? 0
        : Math.max(zoomStart, Math.min(options.zoomWindow?.endIndex ?? maxIndex, maxIndex));
    const slicedRows = totalCategoryCount === 0 ? [] : options.data.slice(zoomStart, zoomEnd + 1);
    const categories = totalCategoryCount === 0 ? [] : allCategories.slice(zoomStart, zoomEnd + 1);
    const hiddenSeriesSet = new Set(options.hiddenSeries || []);
    const referenceLines = normalizeReferenceLines(options.referenceLines);
    const referenceAreas = normalizeReferenceAreas(options.referenceAreas);
    const needsSecondaryAxis = Boolean(options.series.some(item => item.yAxisIndex === 1)
        || referenceLines.some(item => item.axisIndex === 1)
        || referenceAreas.some(item => item.axisIndex === 1)
        || options.yAxes?.[1]);
    const axisConfig = resolveAxisConfig(options, needsSecondaryAxis);
    const baseMargins = resolveMargins(options.margins);
    const resolvedBaseMargins = needsSecondaryAxis
        ? {
            ...baseMargins,
            right: Math.max(baseMargins.right, 56)
        }
        : baseMargins;
    const legendRows = options.showLegend === false
        ? 0
        : resolveLegendRows(options.series.map(item => item.label || item.field), Math.max(120, width - resolvedBaseMargins.left - resolvedBaseMargins.right));
    const headerLayout = resolveHeaderLayout(baseMargins.top, options, legendRows);
    const margins = resolveLayoutMargins(baseMargins, headerLayout, needsSecondaryAxis, orientation);
    const plotWidth = Math.max(1, width - margins.left - margins.right);
    const plotHeight = Math.max(1, height - margins.top - margins.bottom);
    const stackState = new Map();
    const series = options.series.map((item, seriesIndex) => {
        const field = item.field;
        const axisIndex = item.yAxisIndex === 1 ? 1 : 0;
        const label = item.label || field;
        const color = theme.palette[seriesIndex % theme.palette.length];
        const stack = item.stack?.trim() || undefined;
        const visible = !hiddenSeriesSet.has(field);
        const typeForSeries = resolveSeriesType(type, item.type);
        const points = slicedRows.map((row, rowIndex) => {
            const value = toFiniteNumber(row?.[field]);
            let stackStart = 0;
            let stackEnd = value;
            if (visible && stack) {
                const stackKey = `${axisIndex}:${stack}`;
                if (!stackState.has(stackKey)) {
                    stackState.set(stackKey, {
                        positive: Array.from({ length: slicedRows.length }, () => 0),
                        negative: Array.from({ length: slicedRows.length }, () => 0)
                    });
                }
                const state = stackState.get(stackKey);
                if (value >= 0) {
                    stackStart = state.positive[rowIndex];
                    stackEnd = stackStart + value;
                    state.positive[rowIndex] = stackEnd;
                }
                else {
                    stackStart = state.negative[rowIndex];
                    stackEnd = stackStart + value;
                    state.negative[rowIndex] = stackEnd;
                }
            }
            return {
                category: categories[rowIndex] || `项目 ${zoomStart + rowIndex + 1}`,
                categoryIndex: zoomStart + rowIndex,
                value,
                axisIndex,
                stackStart,
                stackEnd
            };
        });
        return {
            field,
            label,
            type: typeForSeries,
            color,
            visible,
            stack,
            smooth: Boolean(item.smooth),
            showSymbol: item.showSymbol !== false,
            showLabel: Boolean(item.showLabel),
            yAxisIndex: axisIndex,
            labelFormatter: item.labelFormatter,
            points
        };
    });
    const yAxes = axisConfig.map((axis, index) => {
        const axisIndex = index === 1 ? 1 : 0;
        const domain = resolveAxisDomain(series, referenceLines, referenceAreas, axis, axisIndex);
        return {
            index: axisIndex,
            position: axisIndex === 0 ? 'left' : 'right',
            name: axis?.name,
            formatter: axis?.formatter,
            minValue: domain.minValue,
            maxValue: domain.maxValue
        };
    });
    const pie = type === 'pie'
        ? buildPieModel(options, series, width, height, margins)
        : undefined;
    const isEmpty = type === 'pie'
        ? !pie || pie.total <= 0 || pie.slices.length === 0
        : categories.length === 0 || series.every(item => !item.visible);
    const primaryAxis = yAxes[0];
    return {
        type,
        orientation,
        width,
        height,
        plotWidth,
        plotHeight,
        margins,
        categories,
        series,
        minValue: primaryAxis.minValue,
        maxValue: primaryAxis.maxValue,
        yAxes,
        referenceLines,
        referenceAreas,
        title: options.title,
        categoryField: options.categoryField,
        theme,
        showLegend: options.showLegend !== false,
        legendRows,
        headerLayout,
        totalCategoryCount,
        visibleStartIndex: zoomStart,
        visibleEndIndex: zoomEnd,
        isEmpty,
        emptyState: normalizeEmptyState(options.emptyState),
        tooltip: {
            shared: type === 'pie' ? false : options.tooltip?.shared !== false,
            crosshair: type === 'pie' ? false : options.tooltip?.crosshair !== false,
            formatter: options.tooltip?.formatter
        },
        pie,
        xAxisFormatter: options.xAxisFormatter
    };
};

ctx["buildChartModel"] = buildChartModel;
})();
(function(){
/* ---- renderSvg.js ---- */
const round = (value) => {
    return Math.round(value * 100) / 100;
};
const esc = (value) => {
    return value
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&apos;');
};
const getAxis = (model, axisIndex) => {
    return model.yAxes.find(item => item.index === axisIndex) || model.yAxes[0];
};
const scaleValue = (model, value, axisIndex) => {
    const axis = getAxis(model, axisIndex);
    const domain = axis.maxValue - axis.minValue;
    const ratio = domain <= 0 ? 0 : (value - axis.minValue) / domain;
    if (model.orientation === 'horizontal') {
        return model.margins.left + ratio * model.plotWidth;
    }
    return model.margins.top + model.plotHeight - ratio * model.plotHeight;
};
const baselinePosition = (model, axisIndex) => {
    return scaleValue(model, 0, axisIndex);
};
const getCategoryStep = (model) => {
    const count = Math.max(1, model.categories.length);
    return model.orientation === 'horizontal'
        ? model.plotHeight / count
        : model.plotWidth / count;
};
const getCategoryCenter = (model, localIndex) => {
    const step = getCategoryStep(model);
    if (model.orientation === 'horizontal') {
        return model.margins.top + localIndex * step + step / 2;
    }
    return model.margins.left + localIndex * step + step / 2;
};
const getSeriesValueLabel = (axis, series, point) => {
    if (series.labelFormatter) {
        return series.labelFormatter(point.value, {
            category: point.category,
            index: point.categoryIndex,
            field: series.field,
            label: series.label
        });
    }
    if (axis.formatter) {
        return axis.formatter(point.value);
    }
    return String(round(point.value));
};
const getTooltipText = (axis, series, point) => {
    return `${series.label} / ${point.category}: ${getSeriesValueLabel(axis, series, point)}`;
};
const formatPieValue = (model, value) => {
    if (model.pie?.valueFormatter) {
        return model.pie.valueFormatter(value);
    }
    return String(round(value));
};
const getPointCoordinates = (model, point) => {
    const localIndex = point.categoryIndex - model.visibleStartIndex;
    if (model.orientation === 'horizontal') {
        return {
            x: scaleValue(model, point.stackEnd, point.axisIndex),
            y: getCategoryCenter(model, localIndex)
        };
    }
    return {
        x: getCategoryCenter(model, localIndex),
        y: scaleValue(model, point.stackEnd, point.axisIndex)
    };
};
const buildLinearPath = (coords) => {
    return coords.map((coord, index) => {
        return `${index === 0 ? 'M' : 'L'} ${round(coord.x)} ${round(coord.y)}`;
    }).join(' ');
};
const buildSmoothPath = (model, coords) => {
    if (coords.length === 0) {
        return '';
    }
    let path = `M ${round(coords[0].x)} ${round(coords[0].y)}`;
    for (let index = 0; index < coords.length - 1; index += 1) {
        const current = coords[index];
        const next = coords[index + 1];
        if (!current || !next) {
            continue;
        }
        const deltaX = next.x - current.x;
        const deltaY = next.y - current.y;
        const cp1 = model.orientation === 'horizontal'
            ? { x: current.x, y: current.y + deltaY / 2 }
            : { x: current.x + deltaX / 2, y: current.y };
        const cp2 = model.orientation === 'horizontal'
            ? { x: next.x, y: current.y + deltaY / 2 }
            : { x: current.x + deltaX / 2, y: next.y };
        path += ` C ${round(cp1.x)} ${round(cp1.y)}, ${round(cp2.x)} ${round(cp2.y)}, ${round(next.x)} ${round(next.y)}`;
    }
    return path;
};
const buildLinePath = (model, series, useStackStart = false) => {
    const coords = series.points.map((point) => {
        if (useStackStart) {
            const localIndex = point.categoryIndex - model.visibleStartIndex;
            if (model.orientation === 'horizontal') {
                return {
                    x: scaleValue(model, point.stackStart, point.axisIndex),
                    y: getCategoryCenter(model, localIndex)
                };
            }
            return {
                x: getCategoryCenter(model, localIndex),
                y: scaleValue(model, point.stackStart, point.axisIndex)
            };
        }
        return getPointCoordinates(model, point);
    });
    if (series.smooth) {
        return buildSmoothPath(model, coords);
    }
    return buildLinearPath(coords);
};
const buildAreaFillPath = (model, series) => {
    const topCoords = series.points.map(point => getPointCoordinates(model, point));
    if (topCoords.length === 0) {
        return '';
    }
    const bottomCoords = series.points
        .map((point) => {
        const localIndex = point.categoryIndex - model.visibleStartIndex;
        if (model.orientation === 'horizontal') {
            return {
                x: scaleValue(model, point.stackStart, point.axisIndex),
                y: getCategoryCenter(model, localIndex)
            };
        }
        return {
            x: getCategoryCenter(model, localIndex),
            y: scaleValue(model, point.stackStart, point.axisIndex)
        };
    })
        .reverse();
    const topPath = series.smooth ? buildSmoothPath(model, topCoords) : buildLinearPath(topCoords);
    const tail = bottomCoords.map(coord => `L ${round(coord.x)} ${round(coord.y)}`).join(' ');
    return `${topPath} ${tail} Z`;
};
const renderTitle = (model) => {
    if (!model.title || !model.headerLayout.titleY) {
        return '';
    }
    return `<text x="${round(model.margins.left)}" y="${round(model.headerLayout.titleY)}" fill="${model.theme.text}" font-size="13" font-weight="600">${esc(model.title)}</text>`;
};
const renderLegend = (model) => {
    if (!model.showLegend || model.series.length === 0 || !model.headerLayout.legendY) {
        return '';
    }
    const startX = model.margins.left;
    const maxX = model.width - model.margins.right;
    const baseY = model.headerLayout.legendY;
    const rowHeight = model.headerLayout.legendRowHeight;
    let row = 0;
    let cursorX = startX;
    return model.series.map((series) => {
        const itemWidth = Math.max(72, series.label.length * 8 + 34);
        if (cursorX > startX && cursorX + itemWidth > maxX) {
            row += 1;
            cursorX = startX;
        }
        const x = cursorX;
        const y = baseY + row * rowHeight;
        cursorX += itemWidth;
        const opacity = series.visible ? 1 : 0.35;
        return [
            `<g class="zan-chart-legend-item" data-field="${esc(series.field)}" style="cursor:pointer;opacity:${opacity}">`,
            `<rect x="${round(x)}" y="${round(y - 9)}" width="10" height="10" rx="2" fill="${series.color}" />`,
            `<text x="${round(x + 16)}" y="${round(y)}" fill="${model.theme.text}" font-size="12">${esc(series.label)}</text>`,
            '</g>'
        ].join('');
    }).join('');
};
const renderVerticalAxes = (model) => {
    const left = model.margins.left;
    const right = model.width - model.margins.right;
    const top = model.margins.top;
    const bottom = model.margins.top + model.plotHeight;
    const primaryAxis = getAxis(model, 0);
    const secondaryAxis = model.yAxes.find(item => item.index === 1);
    const yTicks = 4;
    const grid = Array.from({ length: yTicks + 1 }).map((_, idx) => {
        const ratio = idx / yTicks;
        const value = primaryAxis.minValue + (primaryAxis.maxValue - primaryAxis.minValue) * ratio;
        const y = top + model.plotHeight - ratio * model.plotHeight;
        const display = primaryAxis.formatter ? primaryAxis.formatter(value) : String(round(value));
        return [
            `<line x1="${round(left)}" y1="${round(y)}" x2="${round(right)}" y2="${round(y)}" stroke="${model.theme.grid}" stroke-width="1" />`,
            `<text x="${round(left - 8)}" y="${round(y + 4)}" text-anchor="end" fill="${model.theme.axis}" font-size="10">${esc(display)}</text>`
        ].join('');
    }).join('');
    const secondaryTicks = secondaryAxis
        ? Array.from({ length: yTicks + 1 }).map((_, idx) => {
            const ratio = idx / yTicks;
            const value = secondaryAxis.minValue + (secondaryAxis.maxValue - secondaryAxis.minValue) * ratio;
            const y = top + model.plotHeight - ratio * model.plotHeight;
            const display = secondaryAxis.formatter ? secondaryAxis.formatter(value) : String(round(value));
            return `<text class="zan-chart-axis-right" x="${round(right + 8)}" y="${round(y + 4)}" text-anchor="start" fill="${model.theme.axis}" font-size="10">${esc(display)}</text>`;
        }).join('')
        : '';
    const categoryLabels = model.categories.map((label, idx) => {
        const x = getCategoryCenter(model, idx);
        const absoluteIndex = model.visibleStartIndex + idx;
        const display = model.xAxisFormatter ? model.xAxisFormatter(label, absoluteIndex) : label;
        return `<text x="${round(x)}" y="${round(bottom + 18)}" text-anchor="middle" fill="${model.theme.axis}" font-size="11">${esc(display)}</text>`;
    }).join('');
    const baseline = baselinePosition(model, 0);
    return [
        `<g class="zan-chart-axis">`,
        `<line x1="${round(left)}" y1="${round(bottom)}" x2="${round(right)}" y2="${round(bottom)}" stroke="${model.theme.axis}" stroke-width="1.2" />`,
        `<line x1="${round(left)}" y1="${round(top)}" x2="${round(left)}" y2="${round(bottom)}" stroke="${model.theme.axis}" stroke-width="1.2" />`,
        secondaryAxis
            ? `<line class="zan-chart-axis-right" x1="${round(right)}" y1="${round(top)}" x2="${round(right)}" y2="${round(bottom)}" stroke="${model.theme.axis}" stroke-width="1" />`
            : '',
        baseline > top && baseline < bottom
            ? `<line x1="${round(left)}" y1="${round(baseline)}" x2="${round(right)}" y2="${round(baseline)}" stroke="${model.theme.axis}" stroke-width="1" opacity="0.8" />`
            : '',
        primaryAxis.name
            ? `<text x="${round(left)}" y="${round(top - 10)}" text-anchor="start" fill="${model.theme.axis}" font-size="10">${esc(primaryAxis.name)}</text>`
            : '',
        secondaryAxis?.name
            ? `<text class="zan-chart-axis-right" x="${round(right)}" y="${round(top - 10)}" text-anchor="end" fill="${model.theme.axis}" font-size="10">${esc(secondaryAxis.name)}</text>`
            : '',
        grid,
        secondaryTicks,
        categoryLabels,
        '</g>'
    ].join('');
};
const renderHorizontalAxes = (model) => {
    const left = model.margins.left;
    const right = model.width - model.margins.right;
    const top = model.margins.top;
    const bottom = model.margins.top + model.plotHeight;
    const primaryAxis = getAxis(model, 0);
    const xTicks = 4;
    const grid = Array.from({ length: xTicks + 1 }).map((_, idx) => {
        const ratio = idx / xTicks;
        const value = primaryAxis.minValue + (primaryAxis.maxValue - primaryAxis.minValue) * ratio;
        const x = left + model.plotWidth * ratio;
        const display = primaryAxis.formatter ? primaryAxis.formatter(value) : String(round(value));
        return [
            `<line x1="${round(x)}" y1="${round(top)}" x2="${round(x)}" y2="${round(bottom)}" stroke="${model.theme.grid}" stroke-width="1" />`,
            `<text x="${round(x)}" y="${round(bottom + 18)}" text-anchor="middle" fill="${model.theme.axis}" font-size="10">${esc(display)}</text>`
        ].join('');
    }).join('');
    const categoryLabels = model.categories.map((label, idx) => {
        const y = getCategoryCenter(model, idx);
        const absoluteIndex = model.visibleStartIndex + idx;
        const display = model.xAxisFormatter ? model.xAxisFormatter(label, absoluteIndex) : label;
        return `<text x="${round(left - 8)}" y="${round(y + 4)}" text-anchor="end" fill="${model.theme.axis}" font-size="11">${esc(display)}</text>`;
    }).join('');
    const baseline = baselinePosition(model, 0);
    return [
        `<g class="zan-chart-axis">`,
        `<line x1="${round(left)}" y1="${round(bottom)}" x2="${round(right)}" y2="${round(bottom)}" stroke="${model.theme.axis}" stroke-width="1.2" />`,
        `<line x1="${round(left)}" y1="${round(top)}" x2="${round(left)}" y2="${round(bottom)}" stroke="${model.theme.axis}" stroke-width="1.2" />`,
        baseline > left && baseline < right
            ? `<line x1="${round(baseline)}" y1="${round(top)}" x2="${round(baseline)}" y2="${round(bottom)}" stroke="${model.theme.axis}" stroke-width="1" opacity="0.8" />`
            : '',
        primaryAxis.name
            ? `<text x="${round(right)}" y="${round(bottom + 34)}" text-anchor="end" fill="${model.theme.axis}" font-size="10">${esc(primaryAxis.name)}</text>`
            : '',
        grid,
        categoryLabels,
        '</g>'
    ].join('');
};
const renderAxes = (model) => {
    if (model.isEmpty || model.type === 'pie') {
        return '';
    }
    return model.orientation === 'horizontal'
        ? renderHorizontalAxes(model)
        : renderVerticalAxes(model);
};
const renderReferenceAreas = (model) => {
    if (model.referenceAreas.length === 0 || model.isEmpty || model.type === 'pie') {
        return '';
    }
    return model.referenceAreas.map((area) => {
        if (model.orientation === 'horizontal') {
            const x1 = scaleValue(model, area.start, area.axisIndex);
            const x2 = scaleValue(model, area.end, area.axisIndex);
            const x = Math.min(x1, x2);
            const width = Math.abs(x2 - x1);
            return [
                `<g class="zan-chart-reference-area">`,
                `<rect x="${round(x)}" y="${round(model.margins.top)}" width="${round(width)}" height="${round(model.plotHeight)}" fill="${area.color}" opacity="${area.opacity}" />`,
                area.label
                    ? `<text x="${round(x + width - 6)}" y="${round(model.margins.top + 12)}" text-anchor="end" fill="${area.color}" font-size="10">${esc(area.label)}</text>`
                    : '',
                '</g>'
            ].join('');
        }
        const y1 = scaleValue(model, area.start, area.axisIndex);
        const y2 = scaleValue(model, area.end, area.axisIndex);
        const y = Math.min(y1, y2);
        const height = Math.abs(y2 - y1);
        return [
            `<g class="zan-chart-reference-area">`,
            `<rect x="${round(model.margins.left)}" y="${round(y)}" width="${round(model.plotWidth)}" height="${round(height)}" fill="${area.color}" opacity="${area.opacity}" />`,
            area.label
                ? `<text x="${round(model.width - model.margins.right - 6)}" y="${round(y + 12)}" text-anchor="end" fill="${area.color}" font-size="10">${esc(area.label)}</text>`
                : '',
            '</g>'
        ].join('');
    }).join('');
};
const renderReferenceLines = (model) => {
    if (model.referenceLines.length === 0 || model.isEmpty || model.type === 'pie') {
        return '';
    }
    return model.referenceLines.map((line) => {
        if (model.orientation === 'horizontal') {
            const x = scaleValue(model, line.value, line.axisIndex);
            return [
                `<g class="zan-chart-reference-line">`,
                `<line x1="${round(x)}" y1="${round(model.margins.top)}" x2="${round(x)}" y2="${round(model.margins.top + model.plotHeight)}" stroke="${line.color}" stroke-width="1.5" stroke-dasharray="${esc(line.lineDash)}" />`,
                line.label
                    ? `<text x="${round(x + 6)}" y="${round(model.margins.top + 12)}" text-anchor="start" fill="${line.color}" font-size="10">${esc(line.label)}</text>`
                    : '',
                '</g>'
            ].join('');
        }
        const y = scaleValue(model, line.value, line.axisIndex);
        return [
            `<g class="zan-chart-reference-line">`,
            `<line x1="${round(model.margins.left)}" y1="${round(y)}" x2="${round(model.width - model.margins.right)}" y2="${round(y)}" stroke="${line.color}" stroke-width="1.5" stroke-dasharray="${esc(line.lineDash)}" />`,
            line.label
                ? `<text x="${round(model.width - model.margins.right - 6)}" y="${round(y - 6)}" text-anchor="end" fill="${line.color}" font-size="10">${esc(line.label)}</text>`
                : '',
            '</g>'
        ].join('');
    }).join('');
};
const buildBarSlotMap = (visibleBarSeries) => {
    const slots = new Map();
    visibleBarSeries.forEach((series) => {
        const key = series.stack || `__${series.field}`;
        if (!slots.has(key)) {
            slots.set(key, slots.size);
        }
    });
    return slots;
};
const renderSeriesLabel = (model, series, point, x, y) => {
    if (!series.showLabel) {
        return '';
    }
    const axis = getAxis(model, series.yAxisIndex);
    const label = getSeriesValueLabel(axis, series, point);
    return `<text class="zan-chart-data-label" x="${round(x)}" y="${round(y)}" fill="${model.theme.text}" font-size="10" text-anchor="middle">${esc(label)}</text>`;
};
const renderBarSeries = (model, series, slotIndex, slotCount) => {
    const step = getCategoryStep(model);
    const padding = step * 0.22;
    const slotSpan = Math.max(6, (step - padding) / Math.max(1, slotCount));
    const barBreadth = Math.max(4, slotSpan - 2);
    return series.points.map((point) => {
        const localIndex = point.categoryIndex - model.visibleStartIndex;
        const axis = getAxis(model, series.yAxisIndex);
        const tooltip = getTooltipText(axis, series, point);
        if (model.orientation === 'horizontal') {
            const centerY = getCategoryCenter(model, localIndex);
            const top = centerY - (step - padding) / 2 + slotIndex * slotSpan + (slotSpan - barBreadth) / 2;
            const x1 = scaleValue(model, point.stackStart, point.axisIndex);
            const x2 = scaleValue(model, point.stackEnd, point.axisIndex);
            const x = Math.min(x1, x2);
            const width = Math.max(1, Math.abs(x2 - x1));
            const labelX = point.value >= 0 ? x + width + 14 : x - 14;
            const labelAnchor = point.value >= 0 ? 'middle' : 'middle';
            return [
                `<rect x="${round(x)}" y="${round(top)}" width="${round(width)}" height="${round(barBreadth)}" rx="3" fill="${series.color}" data-tooltip="${esc(tooltip)}" data-series-field="${esc(series.field)}" data-series-label="${esc(series.label)}" data-series-color="${esc(series.color)}" data-category="${esc(point.category)}" data-value="${round(point.value)}" data-index="${point.categoryIndex}" data-category-index="${point.categoryIndex}" />`,
                series.showLabel
                    ? `<text class="zan-chart-data-label" x="${round(labelX)}" y="${round(top + barBreadth / 2 + 4)}" fill="${model.theme.text}" font-size="10" text-anchor="${labelAnchor}">${esc(getSeriesValueLabel(axis, series, point))}</text>`
                    : ''
            ].join('');
        }
        const centerX = getCategoryCenter(model, localIndex);
        const left = centerX - (step - padding) / 2 + slotIndex * slotSpan + (slotSpan - barBreadth) / 2;
        const y1 = scaleValue(model, point.stackStart, point.axisIndex);
        const y2 = scaleValue(model, point.stackEnd, point.axisIndex);
        const y = Math.min(y1, y2);
        const height = Math.max(1, Math.abs(y2 - y1));
        const labelY = point.value >= 0 ? y - 6 : y + height + 12;
        return [
            `<rect x="${round(left)}" y="${round(y)}" width="${round(barBreadth)}" height="${round(height)}" rx="3" fill="${series.color}" data-tooltip="${esc(tooltip)}" data-series-field="${esc(series.field)}" data-series-label="${esc(series.label)}" data-series-color="${esc(series.color)}" data-category="${esc(point.category)}" data-value="${round(point.value)}" data-index="${point.categoryIndex}" data-category-index="${point.categoryIndex}" />`,
            renderSeriesLabel(model, series, point, left + barBreadth / 2, labelY)
        ].join('');
    }).join('');
};
const renderLineSymbols = (model, series) => {
    if (!series.showSymbol && !series.showLabel) {
        return '';
    }
    const axis = getAxis(model, series.yAxisIndex);
    return series.points.map((point) => {
        const coords = getPointCoordinates(model, point);
        const tooltip = getTooltipText(axis, series, point);
        return [
            series.showSymbol
                ? `<circle cx="${round(coords.x)}" cy="${round(coords.y)}" r="3" fill="${series.color}" data-tooltip="${esc(tooltip)}" data-series-field="${esc(series.field)}" data-series-label="${esc(series.label)}" data-series-color="${esc(series.color)}" data-category="${esc(point.category)}" data-value="${round(point.value)}" data-index="${point.categoryIndex}" data-category-index="${point.categoryIndex}" />`
                : '',
            renderSeriesLabel(model, series, point, coords.x, coords.y - 8)
        ].join('');
    }).join('');
};
const renderLineSeries = (model, series) => {
    const path = buildLinePath(model, series);
    return [
        `<path d="${path}" fill="none" stroke="${series.color}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" />`,
        renderLineSymbols(model, series)
    ].join('');
};
const renderAreaSeries = (model, series) => {
    const fillPath = buildAreaFillPath(model, series);
    const linePath = buildLinePath(model, series);
    return [
        `<path d="${fillPath}" fill="${series.color}" opacity="0.16" />`,
        `<path d="${linePath}" fill="none" stroke="${series.color}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" />`,
        renderLineSymbols(model, series)
    ].join('');
};
const polarToCartesian = (centerX, centerY, radius, angle) => {
    return {
        x: centerX + Math.cos(angle) * radius,
        y: centerY + Math.sin(angle) * radius
    };
};
const buildPieSlicePath = (centerX, centerY, outerRadius, innerRadius, startAngle, endAngle) => {
    const safeEndAngle = endAngle - (endAngle - startAngle >= Math.PI * 2 ? 0.0001 : 0);
    const largeArcFlag = safeEndAngle - startAngle > Math.PI ? 1 : 0;
    const outerStart = polarToCartesian(centerX, centerY, outerRadius, startAngle);
    const outerEnd = polarToCartesian(centerX, centerY, outerRadius, safeEndAngle);
    if (innerRadius <= 0) {
        return [
            `M ${round(centerX)} ${round(centerY)}`,
            `L ${round(outerStart.x)} ${round(outerStart.y)}`,
            `A ${round(outerRadius)} ${round(outerRadius)} 0 ${largeArcFlag} 1 ${round(outerEnd.x)} ${round(outerEnd.y)}`,
            'Z'
        ].join(' ');
    }
    const innerEnd = polarToCartesian(centerX, centerY, innerRadius, safeEndAngle);
    const innerStart = polarToCartesian(centerX, centerY, innerRadius, startAngle);
    return [
        `M ${round(outerStart.x)} ${round(outerStart.y)}`,
        `A ${round(outerRadius)} ${round(outerRadius)} 0 ${largeArcFlag} 1 ${round(outerEnd.x)} ${round(outerEnd.y)}`,
        `L ${round(innerEnd.x)} ${round(innerEnd.y)}`,
        `A ${round(innerRadius)} ${round(innerRadius)} 0 ${largeArcFlag} 0 ${round(innerStart.x)} ${round(innerStart.y)}`,
        'Z'
    ].join(' ');
};
const renderPieSeries = (model) => {
    if (!model.pie || model.pie.slices.length === 0) {
        return '';
    }
    const { pie } = model;
    const slices = pie.slices.map((slice) => {
        const path = buildPieSlicePath(pie.centerX, pie.centerY, pie.outerRadius, pie.innerRadius, slice.startAngle, slice.endAngle);
        const midAngle = slice.startAngle + (slice.endAngle - slice.startAngle) / 2;
        const labelRadius = pie.innerRadius > 0
            ? pie.innerRadius + (pie.outerRadius - pie.innerRadius) * 0.62
            : pie.outerRadius * 0.68;
        const labelPoint = polarToCartesian(pie.centerX, pie.centerY, labelRadius, midAngle);
        const valueLabel = formatPieValue(model, slice.value);
        const percentageLabel = `${Math.round(slice.ratio * 100)}%`;
        return [
            `<path d="${path}" fill="${slice.color}" stroke="${model.theme.background}" stroke-width="2" data-tooltip="${esc(`${slice.label}: ${valueLabel} (${percentageLabel})`)}" data-series-field="${esc(slice.field)}" data-series-label="${esc(slice.label)}" data-series-color="${esc(slice.color)}" data-category="${esc(slice.label)}" data-value="${round(slice.value)}" data-index="0" data-category-index="0" />`,
            pie.showSliceLabels && slice.ratio >= 0.06
                ? `<text class="zan-chart-data-label" x="${round(labelPoint.x)}" y="${round(labelPoint.y)}" fill="${model.theme.text}" font-size="10" text-anchor="middle">${esc(percentageLabel)}</text>`
                : ''
        ].join('');
    }).join('');
    const centerText = pie.donut
        ? [
            pie.totalLabel
                ? `<text x="${round(pie.centerX)}" y="${round(pie.centerY - 4)}" text-anchor="middle" fill="${model.theme.axis}" font-size="11">${esc(pie.totalLabel)}</text>`
                : '',
            `<text x="${round(pie.centerX)}" y="${round(pie.centerY + (pie.totalLabel ? 18 : 6))}" text-anchor="middle" fill="${model.theme.text}" font-size="18" font-weight="700">${esc(formatPieValue(model, pie.total))}</text>`
        ].join('')
        : '';
    return `${slices}${centerText}`;
};
const renderSeries = (model) => {
    if (model.isEmpty) {
        return '';
    }
    if (model.type === 'pie') {
        return renderPieSeries(model);
    }
    const visibleSeries = model.series.filter(item => item.visible);
    if (visibleSeries.length === 0) {
        return '';
    }
    const useSeriesType = model.type === 'combo';
    const visibleBarSeries = visibleSeries.filter((series) => {
        const seriesType = useSeriesType ? series.type : model.type;
        return seriesType === 'bar';
    });
    const barSlots = buildBarSlotMap(visibleBarSeries);
    const bars = visibleBarSeries.map((series) => {
        const slotKey = series.stack || `__${series.field}`;
        return renderBarSeries(model, series, barSlots.get(slotKey) || 0, Math.max(1, barSlots.size));
    }).join('');
    const areas = visibleSeries
        .filter((series) => {
        const seriesType = useSeriesType ? series.type : model.type;
        return seriesType === 'area';
    })
        .map(series => renderAreaSeries(model, series))
        .join('');
    const lines = visibleSeries
        .filter((series) => {
        const seriesType = useSeriesType ? series.type : model.type;
        return seriesType === 'line';
    })
        .map(series => renderLineSeries(model, series))
        .join('');
    return `${bars}${areas}${lines}`;
};
const renderEmptyState = (model) => {
    if (!model.isEmpty) {
        return '';
    }
    const centerX = model.margins.left + model.plotWidth / 2;
    const centerY = model.margins.top + model.plotHeight / 2;
    const frameWidth = Math.min(220, model.plotWidth - 24);
    const frameHeight = 72;
    return [
        `<g class="zan-chart-empty-state">`,
        `<rect x="${round(centerX - frameWidth / 2)}" y="${round(centerY - frameHeight / 2)}" width="${round(frameWidth)}" height="${round(frameHeight)}" rx="12" fill="${model.theme.background}" stroke="${model.theme.border}" stroke-dasharray="6 4" />`,
        `<text x="${round(centerX)}" y="${round(centerY - 4)}" text-anchor="middle" fill="${model.theme.text}" font-size="13" font-weight="600">${esc(model.emptyState.text)}</text>`,
        model.emptyState.subtext
            ? `<text x="${round(centerX)}" y="${round(centerY + 18)}" text-anchor="middle" fill="${model.theme.axis}" font-size="11">${esc(model.emptyState.subtext)}</text>`
            : '',
        `</g>`
    ].join('');
};
const renderBrush = (model) => {
    if (model.type === 'pie' || model.totalCategoryCount <= 1 || model.orientation === 'horizontal') {
        return '';
    }
    const trackX = model.margins.left;
    const trackY = model.height - 10;
    const trackWidth = model.plotWidth;
    const trackHeight = 6;
    const startRatio = model.visibleStartIndex / Math.max(1, model.totalCategoryCount - 1);
    const endRatio = model.visibleEndIndex / Math.max(1, model.totalCategoryCount - 1);
    const selectionX = trackX + trackWidth * startRatio;
    const selectionWidth = Math.max(6, trackWidth * (endRatio - startRatio || 1));
    const handleWidth = 5;
    const leftHandleX = selectionX - handleWidth / 2;
    const rightHandleX = selectionX + selectionWidth - handleWidth / 2;
    return [
        `<g class="zan-chart-brush">`,
        `<rect class="zan-chart-brush-track" x="${round(trackX)}" y="${round(trackY)}" width="${round(trackWidth)}" height="${trackHeight}" rx="3" fill="${model.theme.grid}" style="cursor:crosshair" />`,
        `<rect class="zan-chart-brush-selection" x="${round(selectionX)}" y="${round(trackY)}" width="${round(selectionWidth)}" height="${trackHeight}" rx="3" fill="${model.theme.palette[0] || '#60a5fa'}" fill-opacity="0.75" data-start="${model.visibleStartIndex}" data-end="${model.visibleEndIndex}" style="cursor:grab" />`,
        `<rect class="zan-chart-brush-handle-left" x="${round(leftHandleX)}" y="${round(trackY - 1)}" width="${handleWidth}" height="${trackHeight + 2}" rx="2" fill="${model.theme.palette[0] || '#2563eb'}" data-start="${model.visibleStartIndex}" data-end="${model.visibleEndIndex}" style="cursor:ew-resize" />`,
        `<rect class="zan-chart-brush-handle-right" x="${round(rightHandleX)}" y="${round(trackY - 1)}" width="${handleWidth}" height="${trackHeight + 2}" rx="2" fill="${model.theme.palette[0] || '#2563eb'}" data-start="${model.visibleStartIndex}" data-end="${model.visibleEndIndex}" style="cursor:ew-resize" />`,
        `</g>`
    ].join('');
};
const renderChartSvg = (model) => {
    return [
        `<svg width="${model.width}" height="${model.height}" viewBox="0 0 ${model.width} ${model.height}" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="赞图表" data-orientation="${model.orientation}">`,
        `<rect x="0" y="0" width="${model.width}" height="${model.height}" fill="${model.theme.background}" stroke="${model.theme.border}" rx="8" />`,
        renderTitle(model),
        renderLegend(model),
        renderReferenceAreas(model),
        renderAxes(model),
        renderReferenceLines(model),
        renderSeries(model),
        renderEmptyState(model),
        renderBrush(model),
        '</svg>'
    ].join('');
};

ctx["renderChartSvg"] = renderChartSvg;
})();
(function(){
/* ---- index.js ---- */
var defaultTheme = ctx["defaultTheme"];
var buildChartModel = ctx["buildChartModel"];
var renderChartSvg = ctx["renderChartSvg"];
var resolveItemTooltipContent = ctx["resolveItemTooltipContent"];
var resolveSharedTooltipContent = ctx["resolveSharedTooltipContent"];
ctx["buildChartModel"] = ctx["buildChartModel"];
ctx["renderChartSvg"] = ctx["renderChartSvg"];
const ensureHost = (container) => {
    if (!container.style.position) {
        container.style.position = 'relative';
    }
};
const resolveContainerSize = (container) => {
    const rect = container.getBoundingClientRect();
    const computedStyle = typeof window !== 'undefined' ? window.getComputedStyle(container) : null;
    const computedWidth = Math.floor(Number.parseFloat(computedStyle?.width || '0') || 0);
    const computedHeight = Math.floor(Number.parseFloat(computedStyle?.height || '0') || 0);
    const boxWidth = Math.max(Math.floor(container.clientWidth || 0), Math.floor(container.offsetWidth || 0), computedWidth, Math.floor(rect.width || 0));
    const boxHeight = Math.max(Math.floor(container.clientHeight || 0), Math.floor(container.offsetHeight || 0), computedHeight, Math.floor(rect.height || 0));
    return {
        width: Math.max(240, boxWidth || 640),
        height: boxHeight > 0 ? boxHeight : 320
    };
};
const renderIntoHost = (host, options) => {
    const model = buildChartModel(options);
    host.innerHTML = renderChartSvg(model);
    return model;
};
const normalizeZoomWindow = (dataLength, input) => {
    const maxIndex = Math.max(0, dataLength - 1);
    const start = Math.max(0, Math.min(input?.startIndex ?? 0, maxIndex));
    const end = Math.max(start, Math.min(input?.endIndex ?? maxIndex, maxIndex));
    return {
        startIndex: start,
        endIndex: end
    };
};
const buildSvgDataUrl = (svg) => {
    return `data:image/svg+xml;charset=utf-8,${encodeURIComponent(svg)}`;
};
const exportSvgToPngDataUrl = (svg) => {
    return new Promise((resolve, reject) => {
        if (typeof window === 'undefined' || typeof document === 'undefined') {
            reject(new Error('PNG 导出需要浏览器环境'));
            return;
        }
        const image = new Image();
        const svgDataUrl = buildSvgDataUrl(svg);
        image.onload = () => {
            const canvas = document.createElement('canvas');
            canvas.width = Math.max(1, image.width);
            canvas.height = Math.max(1, image.height);
            const context = canvas.getContext('2d');
            if (!context) {
                reject(new Error('无法获取 Canvas 上下文'));
                return;
            }
            context.drawImage(image, 0, 0);
            resolve(canvas.toDataURL('image/png'));
        };
        image.onerror = () => {
            reject(new Error('图表图像渲染失败'));
        };
        image.src = svgDataUrl;
    });
};
const triggerDownload = (filename, dataUrl) => {
    if (typeof document === 'undefined') {
        return;
    }
    const link = document.createElement('a');
    link.href = dataUrl;
    link.download = filename;
    link.click();
};
const getCategoryStep = (model) => {
    const count = Math.max(1, model.categories.length);
    return model.orientation === 'horizontal'
        ? model.plotHeight / count
        : model.plotWidth / count;
};
const getCategoryCrosshairPosition = (model, absoluteIndex) => {
    const localIndex = absoluteIndex - model.visibleStartIndex;
    const step = getCategoryStep(model);
    if (model.orientation === 'horizontal') {
        return {
            top: model.margins.top + localIndex * step + step / 2
        };
    }
    return {
        left: model.margins.left + localIndex * step + step / 2
    };
};
const rgbVarToColor = (styles, name, fallback) => {
    const value = styles.getPropertyValue(name).trim();
    return value ? `rgb(${value})` : fallback;
};
const rgbaVarToColor = (styles, name, alpha, fallback) => {
    const value = styles.getPropertyValue(name).trim();
    if (!value) {
        return fallback;
    }
    const parts = value.split(/\s+/).slice(0, 3);
    if (parts.length < 3) {
        return fallback;
    }
    return `rgba(${parts[0]}, ${parts[1]}, ${parts[2]}, ${alpha})`;
};
const resolveContainerTheme = (container, theme) => {
    const styles = getComputedStyle(container);
    return {
        background: theme?.background || rgbVarToColor(styles, '--container-bg-color', '#ffffff'),
        border: theme?.border || rgbaVarToColor(styles, '--base-text-color', 0.12, '#e5e7eb'),
        text: theme?.text || rgbVarToColor(styles, '--base-text-color', '#111827'),
        axis: theme?.axis || rgbaVarToColor(styles, '--base-text-color', 0.64, '#6b7280'),
        grid: theme?.grid || rgbaVarToColor(styles, '--base-text-color', 0.08, '#f3f4f6'),
        palette: theme?.palette && theme.palette.length > 0 ? [...theme.palette] : [...defaultTheme.palette]
    };
};
const createChart = (container, options) => {
    ensureHost(container);
    const host = document.createElement('div');
    host.style.cssText = 'width:100%;height:100%;box-sizing:border-box;';
    container.appendChild(host);
    const tooltip = document.createElement('div');
    tooltip.style.cssText = [
        'position:absolute',
        'pointer-events:none',
        'background:rgba(17,24,39,0.94)',
        'color:#fff',
        'font:12px/1.45 -apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif',
        'padding:8px 10px',
        'border-radius:8px',
        'transform:translate(-50%,-120%)',
        'display:none',
        'z-index:10',
        'min-width:96px',
        'max-width:240px',
        'white-space:normal',
        'box-shadow:0 12px 30px rgba(15,23,42,0.28)'
    ].join(';');
    container.appendChild(tooltip);
    const crosshair = document.createElement('div');
    crosshair.style.cssText = [
        'position:absolute',
        'display:none',
        'pointer-events:none',
        'z-index:9',
        'background:rgba(59,130,246,0.35)'
    ].join(';');
    container.appendChild(crosshair);
    const toolbar = document.createElement('div');
    toolbar.style.cssText = [
        'position:absolute',
        'right:8px',
        'top:8px',
        'display:flex',
        'gap:6px',
        'z-index:11'
    ].join(';');
    container.appendChild(toolbar);
    let themeOverrides = {
        ...(options.theme || {})
    };
    let currentOptions = {
        ...options,
        theme: resolveContainerTheme(container, themeOverrides),
        hiddenSeries: [...(options.hiddenSeries || [])],
        zoomWindow: normalizeZoomWindow(options.data?.length || 0, options.zoomWindow)
    };
    if (currentOptions.autoFit !== false) {
        currentOptions = {
            ...currentOptions,
            ...resolveContainerSize(container)
        };
    }
    let currentModel = buildChartModel(currentOptions);
    let resizeObserver = null;
    let themeObserver = null;
    const createToolbarButton = (text, onClick) => {
        const button = document.createElement('button');
        button.type = 'button';
        button.textContent = text;
        button.style.cssText = [
            `border:1px solid ${currentOptions.theme?.border || '#d1d5db'}`,
            `background:${currentOptions.theme?.background || '#fff'}`,
            `color:${currentOptions.theme?.text || '#111827'}`,
            'font:12px/1.2 -apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif',
            'padding:4px 8px',
            'border-radius:4px',
            'cursor:pointer'
        ].join(';');
        button.addEventListener('click', (event) => {
            event.preventDefault();
            onClick();
        });
        return button;
    };
    const hideTooltip = () => {
        tooltip.style.display = 'none';
        crosshair.style.display = 'none';
    };
    const updateCrosshair = (absoluteIndex) => {
        if (!currentModel.tooltip.crosshair || currentModel.isEmpty) {
            crosshair.style.display = 'none';
            return;
        }
        const position = getCategoryCrosshairPosition(currentModel, absoluteIndex);
        crosshair.style.display = 'block';
        if (currentModel.orientation === 'horizontal') {
            crosshair.style.left = `${currentModel.margins.left}px`;
            crosshair.style.top = `${position.top || 0}px`;
            crosshair.style.width = `${currentModel.plotWidth}px`;
            crosshair.style.height = '1px';
            crosshair.style.transform = 'translateY(-0.5px)';
        }
        else {
            crosshair.style.left = `${position.left || 0}px`;
            crosshair.style.top = `${currentModel.margins.top}px`;
            crosshair.style.width = '1px';
            crosshair.style.height = `${currentModel.plotHeight}px`;
            crosshair.style.transform = 'translateX(-0.5px)';
        }
    };
    const bindLegendAndTooltip = () => {
        const legendItems = host.querySelectorAll('.zan-chart-legend-item');
        legendItems.forEach((item) => {
            const field = item.getAttribute('data-field') || '';
            item.addEventListener('click', () => {
                const hiddenSet = new Set(currentOptions.hiddenSeries || []);
                const nextHidden = new Set(hiddenSet);
                const isHidden = nextHidden.has(field);
                if (isHidden) {
                    nextHidden.delete(field);
                }
                else {
                    nextHidden.add(field);
                }
                currentOptions = {
                    ...currentOptions,
                    hiddenSeries: [...nextHidden]
                };
                currentOptions.hooks?.onLegendToggle?.(field, !isHidden);
                render();
            });
        });
        const svg = host.querySelector('svg');
        const pointNodes = host.querySelectorAll('[data-category-index]');
        pointNodes.forEach((node) => {
            node.addEventListener('click', (event) => {
                const category = node.getAttribute('data-category') || '';
                const value = Number(node.getAttribute('data-value') || 0);
                const field = node.getAttribute('data-series-field') || '';
                const label = node.getAttribute('data-series-label') || field;
                const index = Number(node.getAttribute('data-index') || -1);
                currentOptions.hooks?.onPointClick?.({
                    category,
                    value,
                    field,
                    label,
                    index,
                    shiftKey: !!event.shiftKey,
                    ctrlKey: !!event.ctrlKey,
                    metaKey: !!event.metaKey
                });
            });
        });
        const onMove = (event) => {
            const target = event.target;
            const pointNode = target?.closest?.('[data-category-index]');
            if (!pointNode) {
                hideTooltip();
                return;
            }
            const absoluteIndex = Number(pointNode.getAttribute('data-category-index') || -1);
            if (absoluteIndex < 0) {
                hideTooltip();
                return;
            }
            const rect = container.getBoundingClientRect();
            const tooltipContent = currentModel.tooltip.shared
                ? resolveSharedTooltipContent(currentModel, absoluteIndex)
                : resolveItemTooltipContent(currentModel, {
                    field: pointNode.getAttribute('data-series-field') || '',
                    categoryIndex: absoluteIndex
                });
            if (!tooltipContent?.content) {
                hideTooltip();
                return;
            }
            if (tooltipContent.isHtml) {
                tooltip.innerHTML = tooltipContent.content;
            }
            else {
                tooltip.textContent = tooltipContent.content;
            }
            tooltip.style.display = 'block';
            tooltip.style.left = `${event.clientX - rect.left}px`;
            tooltip.style.top = `${event.clientY - rect.top}px`;
            updateCrosshair(absoluteIndex);
        };
        const brushTrack = host.querySelector('.zan-chart-brush-track');
        const brush = host.querySelector('.zan-chart-brush-selection');
        const leftHandle = host.querySelector('.zan-chart-brush-handle-left');
        const rightHandle = host.querySelector('.zan-chart-brush-handle-right');
        if (brush && currentOptions.enableZoom) {
            if (brushTrack) {
                brushTrack.addEventListener('mousedown', (event) => {
                    event.preventDefault();
                    const svgRect = svg?.getBoundingClientRect();
                    if (!svgRect || !currentOptions.data || currentOptions.data.length === 0) {
                        return;
                    }
                    const total = Math.max(1, currentOptions.data.length - 1);
                    const startRatio = (event.clientX - svgRect.left) / Math.max(1, svgRect.width);
                    const startIndex = Math.max(0, Math.min(total, Math.round(startRatio * total)));
                    let endIndex = startIndex;
                    const onMoveDrag = (moveEvent) => {
                        const ratio = (moveEvent.clientX - svgRect.left) / Math.max(1, svgRect.width);
                        endIndex = Math.max(0, Math.min(total, Math.round(ratio * total)));
                        const nextStart = Math.min(startIndex, endIndex);
                        const nextEnd = Math.max(startIndex, endIndex);
                        currentOptions = {
                            ...currentOptions,
                            zoomWindow: {
                                startIndex: nextStart,
                                endIndex: Math.max(nextStart + 1, nextEnd)
                            }
                        };
                        currentOptions.hooks?.onZoomChange?.(currentOptions.zoomWindow.startIndex, currentOptions.zoomWindow.endIndex);
                        render();
                    };
                    const onUpDrag = () => {
                        window.removeEventListener('mousemove', onMoveDrag);
                        window.removeEventListener('mouseup', onUpDrag);
                    };
                    window.addEventListener('mousemove', onMoveDrag);
                    window.addEventListener('mouseup', onUpDrag);
                });
            }
            brush.addEventListener('mousedown', (event) => {
                event.preventDefault();
                const startClientX = event.clientX;
                const start = currentOptions.zoomWindow?.startIndex ?? 0;
                const end = currentOptions.zoomWindow?.endIndex ?? Math.max(0, (currentOptions.data?.length || 1) - 1);
                const total = Math.max(1, (currentOptions.data?.length || 1) - 1);
                const range = Math.max(1, end - start);
                const onMoveDrag = (moveEvent) => {
                    const rect = svg?.getBoundingClientRect();
                    if (!rect) {
                        return;
                    }
                    const deltaPx = moveEvent.clientX - startClientX;
                    const ratio = deltaPx / Math.max(1, rect.width);
                    const deltaIndex = Math.round(ratio * total);
                    const nextStart = Math.max(0, Math.min(total - range, start + deltaIndex));
                    const nextEnd = Math.max(nextStart + range, Math.min(total, end + deltaIndex));
                    currentOptions = {
                        ...currentOptions,
                        zoomWindow: {
                            startIndex: nextStart,
                            endIndex: nextEnd
                        }
                    };
                    currentOptions.hooks?.onZoomChange?.(nextStart, nextEnd);
                    render();
                };
                const onUpDrag = () => {
                    window.removeEventListener('mousemove', onMoveDrag);
                    window.removeEventListener('mouseup', onUpDrag);
                };
                window.addEventListener('mousemove', onMoveDrag);
                window.addEventListener('mouseup', onUpDrag);
            });
            const bindHandleDrag = (handle, mode) => {
                if (!handle) {
                    return;
                }
                handle.addEventListener('mousedown', (event) => {
                    event.preventDefault();
                    const startClientX = event.clientX;
                    const initial = normalizeZoomWindow(currentOptions.data?.length || 0, currentOptions.zoomWindow);
                    const total = Math.max(1, (currentOptions.data?.length || 1) - 1);
                    const onMoveDrag = (moveEvent) => {
                        const rect = svg?.getBoundingClientRect();
                        if (!rect) {
                            return;
                        }
                        const deltaPx = moveEvent.clientX - startClientX;
                        const ratio = deltaPx / Math.max(1, rect.width);
                        const deltaIndex = Math.round(ratio * total);
                        let nextStart = initial.startIndex;
                        let nextEnd = initial.endIndex;
                        if (mode === 'left') {
                            nextStart = Math.max(0, Math.min(initial.endIndex - 1, initial.startIndex + deltaIndex));
                        }
                        else {
                            nextEnd = Math.max(initial.startIndex + 1, Math.min(total, initial.endIndex + deltaIndex));
                        }
                        currentOptions = {
                            ...currentOptions,
                            zoomWindow: {
                                startIndex: nextStart,
                                endIndex: nextEnd
                            }
                        };
                        currentOptions.hooks?.onZoomChange?.(nextStart, nextEnd);
                        render();
                    };
                    const onUpDrag = () => {
                        window.removeEventListener('mousemove', onMoveDrag);
                        window.removeEventListener('mouseup', onUpDrag);
                    };
                    window.addEventListener('mousemove', onMoveDrag);
                    window.addEventListener('mouseup', onUpDrag);
                });
            };
            bindHandleDrag(leftHandle, 'left');
            bindHandleDrag(rightHandle, 'right');
        }
        svg?.addEventListener('mousemove', onMove);
        svg?.addEventListener('mouseleave', hideTooltip);
    };
    const render = () => {
        currentOptions = {
            ...currentOptions,
            theme: resolveContainerTheme(container, themeOverrides)
        };
        if (currentOptions.autoFit !== false) {
            currentOptions = {
                ...currentOptions,
                ...resolveContainerSize(container)
            };
        }
        currentModel = renderIntoHost(host, currentOptions);
        bindLegendAndTooltip();
        toolbar.innerHTML = '';
        const toolbarOptions = currentOptions.toolbar;
        const enableToolbar = toolbarOptions?.enabled !== false;
        if (!enableToolbar) {
            return;
        }
        if (currentModel.type !== 'pie' && toolbarOptions?.showResetZoom !== false) {
            toolbar.appendChild(createToolbarButton('重置缩放', () => {
                toolbarOptions?.onResetZoom?.();
                chartHandle.resetZoom();
            }));
        }
        if (toolbarOptions?.showExportPng !== false) {
            toolbar.appendChild(createToolbarButton('PNG', () => {
                const filename = toolbarOptions?.pngFilename || 'zan-chart.png';
                toolbarOptions?.onDownload?.({
                    filename,
                    type: 'image/png'
                });
                void chartHandle.download(filename, 'image/png');
            }));
        }
        if (toolbarOptions?.showExportSvg !== false) {
            toolbar.appendChild(createToolbarButton('SVG', () => {
                const filename = toolbarOptions?.svgFilename || 'zan-chart.svg';
                toolbarOptions?.onDownload?.({
                    filename,
                    type: 'image/svg+xml'
                });
                void chartHandle.download(filename, 'image/svg+xml');
            }));
        }
    };
    if (typeof ResizeObserver !== 'undefined' && options.autoFit !== false) {
        resizeObserver = new ResizeObserver(() => {
            render();
        });
        resizeObserver.observe(container);
    }
    if (typeof MutationObserver !== 'undefined' && typeof document !== 'undefined') {
        themeObserver = new MutationObserver(() => {
            render();
        });
        themeObserver.observe(document.documentElement, {
            attributes: true,
            attributeFilter: ['class', 'style', 'data-theme']
        });
    }
    render();
    const chartHandle = {
        update(next) {
            if (next.theme) {
                themeOverrides = {
                    ...themeOverrides,
                    ...next.theme
                };
            }
            currentOptions = {
                ...currentOptions,
                ...next,
                theme: resolveContainerTheme(container, themeOverrides),
                data: next.data || currentOptions.data,
                series: next.series || currentOptions.series,
                hiddenSeries: next.hiddenSeries || currentOptions.hiddenSeries,
                zoomWindow: normalizeZoomWindow((next.data || currentOptions.data)?.length || 0, next.zoomWindow || currentOptions.zoomWindow)
            };
            render();
        },
        resetZoom() {
            currentOptions = {
                ...currentOptions,
                zoomWindow: normalizeZoomWindow(currentOptions.data?.length || 0)
            };
            currentOptions.hooks?.onZoomChange?.(currentOptions.zoomWindow.startIndex, currentOptions.zoomWindow.endIndex);
            render();
        },
        toSvgString() {
            return host.querySelector('svg')?.outerHTML || '';
        },
        async toDataUrl(type = 'image/png') {
            const svg = host.querySelector('svg')?.outerHTML || '';
            if (!svg) {
                throw new Error('图表 SVG 为空');
            }
            if (type === 'image/svg+xml') {
                return buildSvgDataUrl(svg);
            }
            return exportSvgToPngDataUrl(svg);
        },
        async download(filename = 'zan-chart.png', type = 'image/png') {
            const dataUrl = await this.toDataUrl(type);
            const normalized = filename || (type === 'image/svg+xml' ? 'zan-chart.svg' : 'zan-chart.png');
            triggerDownload(normalized, dataUrl);
        },
        destroy() {
            hideTooltip();
            resizeObserver?.disconnect();
            resizeObserver = null;
            themeObserver?.disconnect();
            themeObserver = null;
            tooltip.remove();
            crosshair.remove();
            toolbar.remove();
            host.remove();
        }
    };
    return chartHandle;
};

ctx["createChart"] = createChart;
})();
exports["defaultTheme"] = ctx["defaultTheme"];
exports["defaultMargins"] = ctx["defaultMargins"];
exports["resolveItemTooltipContent"] = ctx["resolveItemTooltipContent"];
exports["resolveSharedTooltipContent"] = ctx["resolveSharedTooltipContent"];
exports["buildChartModel"] = ctx["buildChartModel"];
exports["renderChartSvg"] = ctx["renderChartSvg"];
exports["createChart"] = ctx["createChart"];
})(window.ZanCharts = window.ZanCharts || {});

import * as d3 from 'd3';

class Data {
	constructor(x, values, score, colour, sigma) {
		values.sort((a, b) => a - b);

		this.x = x;
		this.values = values;
		this.score = score;
		this.colour = colour;
		this.sigma = sigma;

		const [min, max] = values.map(d => [d, d])
			.reduce((a, b) =>
				[
					Math.min(a[0], b[0]),
					Math.max(a[1], b[1])
				]);

		this.min = min;
		this.max = max;

		this.quartiles = [
			d3.quantile(values, 0.25),
			d3.quantile(values, 0.50),
			d3.quantile(values, 0.75)
		];

		const iqr = this.quartiles[2] - this.quartiles[0];

		this.whiskers = [
			Math.max(min, this.quartiles[0] - iqr * 1.5),
			Math.min(max, this.quartiles[2] + iqr * 1.5)
		];

		this.range = [
			Math.min(score, Math.max(min, this.quartiles[0] - iqr * 3)),
			Math.max(score, Math.min(max, this.quartiles[2] + iqr * 3))
		];
	}

	outliers(range) {
		return this.values
			.filter(v => (v > range[0] && v < this.whiskers[0]) || (v < range[1] && v > this.whiskers[1]));
	}

	length() {
		return this.values.length;
	}
}

function BoxPlot(width, height, title, yLabel, data, range) {

	const margin = ({ top: 40, right: 20, bottom: 50, left: 40 });

	const x = d3.scaleBand()
		.domain(data.map(d => d.x))
		.rangeRound([margin.left, width - margin.right])
		.padding(0.25, 0.25);

	const xAxis = g => g
		.attr("transform", `translate(0,${height - margin.bottom})`)
		.call(d3.axisBottom(x).ticks(2).tickSizeOuter(0));

	const y = d3.scaleLinear()
		.domain(range)
		.nice()
		.range([height - margin.bottom, margin.top]);

	const yAxis = g => g
		.attr("transform", `translate(${margin.left},0)`)
		.call(d3.axisLeft(y).ticks(null, "s"))
		.call(g => g.select(".domain").remove());

	const svg = d3.select("body").append("svg")
		.attr("width", width)
		.attr("height", height)
		.attr("class", "box");

	svg.append("text")
		.attr("x", width / 2)
		.attr("y", margin.top / 2)
		.attr("text-anchor", "middle")
		.attr("font-size", "large")
		.text(title);

	const g = svg.append("g")
		.selectAll("g")
		.data(data)
		.enter()
		.append("g");

	g.append("line")
		.attr("stroke", "black")
		.style("stroke-dasharray", "4,2")
		.attr("x1", d => x(d.x) + x.bandwidth() / 2)
		.attr("x2", d => x(d.x) + x.bandwidth() / 2)
		.attr("y1", d => y(d.whiskers[0]))
		.attr("y2", d => y(d.whiskers[1]));

	g.append("line")
		.attr("stroke", "black")
		.attr("x1", d => x(d.x) + x.bandwidth() / 4)
		.attr("x2", d => x(d.x) + 3 * x.bandwidth() / 4)
		.attr("y1", d => y(d.whiskers[0]))
		.attr("y2", d => y(d.whiskers[0]));

	g.append("line")
		.attr("stroke", "black")
		.attr("x1", d => x(d.x) + x.bandwidth() / 4)
		.attr("x2", d => x(d.x) + 3 * x.bandwidth() / 4)
		.attr("y1", d => y(d.whiskers[1]))
		.attr("y2", d => y(d.whiskers[1]));

	g.append("rect")
		.attr("fill", "#fff")
		.attr("stroke", "black")
		.attr("x", d => x(d.x))
		.attr("y", d => y(d.quartiles[2]))
		.attr("width", x.bandwidth())
		.attr("height", d => y(d.quartiles[0]) - y(d.quartiles[2]));

	g.append("line")
		.attr("stroke", "black")
		.attr("stroke-width", 2)
		.attr("x1", d => x(d.x))
		.attr("x2", d => x(d.x) + x.bandwidth())
		.attr("y1", d => y(d.quartiles[1]))
		.attr("y2", d => y(d.quartiles[1]));

	g.append("g")
		.attr("fill", "black")
		.attr("fill-opacity", 0.2)
		.attr("stroke", "none")
		.attr("transform", d => `translate(${x(d.x) + x.bandwidth() / 2},0)`)
		.selectAll("circle")
		.data(d => d.outliers(range))
		.enter()
		.append("circle")
		.attr("r", 1.5)
		.attr("cx", () => (Math.random() - 0.5) * 4)
		.attr("cy", d => y(d));

	svg.selectAll("rect.score")
		.data(data)
		.enter()
		.append("rect")
		.attr("class", "score")
		.attr("fill", d => d.colour)
		.attr("opacity", 0.2)
		.attr("x", margin.left)
		.attr("width", width - margin.left - margin.right)
		.attr("y", d => y(d.score + d.sigma))
		.attr("height", d => Math.abs(y(d.score + d.sigma) - y(d.score - d.sigma)));

	svg.selectAll("line.score")
		.data(data)
		.enter()
		.append("line")
		.attr("class", "score")
		.attr("stroke", d => d.colour)
		// .attr("stroke-width", 1)
		.attr("x1", margin.left)
		.attr("x2", width - margin.right)
		// .attr("x1", d => x(d.x) - x.paddingInner() / 2)
		// .attr("x2", d => x(d.x) - x.paddingInner() / 2 + x.bandwidth())
		.attr("y1", d => y(d.score))
		.attr("y2", d => y(d.score));

	svg.append("g")
		.call(xAxis);

	svg.append("g")
		.call(yAxis);

	svg.append("text")
		.attr("transform", "rotate(-90)")
		.attr("y", 6)
		.attr("x", -(height / 2))
		.attr("text-anchor", "middle")
		// .attr("x", -margin.top)
		// .attr("text-anchor", "end")
		.attr("dy", "0.7em")
		.attr("font-size", "small")
		.text(yLabel);

	svg.append("text")
		.attr("text-anchor", "end")
		.attr("x", width - 6)
		.attr("y", height)
		.attr("dy", "-0.7em")
		.attr("font-size", "xx-small")
		.text(`N=${data[0].length()}`);

	const lg = svg.append("g")
		.attr("transform", `translate(10,${height - margin.bottom + 22})`)
		.selectAll("g.legend")
		.data(data)
		.enter()
		.append("g")
		.attr("class", "legend");

	const ly = d3.scaleBand()
		.domain(data.map(d => d.x))
		.rangeRound([0, margin.bottom - 24])
		.padding(0.2, 0.1);

	lg.append("line")
		.attr("stroke", d => d.colour)
		.attr("x1", 0)
		.attr("x2", 20)
		.attr("y1", d => ly(d.x) + ly.bandwidth() / 2)
		.attr("y2", d => ly(d.x) + ly.bandwidth() / 2);

	lg.append("text")
		.attr("x", 25)
		.attr("y", d => ly(d.x) + ly.bandwidth() / 2)
		.attr("dy", "0.3em")
		.attr("font-size", "xx-small")
		.text(d => d.x);

	return svg.node();
}

function createBoxPlots(e, s, td) {

	const dataRFree = [
		new Data('PDB', s.map(d => 100 * d.RFREE), 100 * e.RFREE, 'blue', 100 * e.SIGRFCAL),
		new Data('PDB-REDO', s.map(d => 100 * d.RFFIN), 100 * e.RFFIN, 'orange', 100 * e.SIGRFFIN)
	];
	const range = dataRFree.map(d => d.range)
		.reduce((a, b) => [Math.min(a[0], b[0]), Math.max(a[1], b[1])]);

	td.appendChild(
		BoxPlot(225, 500, "R-Free", "%", dataRFree, range));

	if (e.OZRAMA != null && e.FZRAMA != null && e.FCHI12 != null && e.OSCHI12 != null) {
		const dataRama = [
			new Data('PDB', s.map(d => d.OZRAMA), e.OZRAMA, 'blue', e.OSZRAMA),
			new Data('PDB-REDO', s.map(d => d.FZRAMA), e.FZRAMA, 'orange', e.FSZRAMA)
		];

		const dataRota = [
			new Data('PDB', s.map(d => d.OCHI12), e.OCHI12, 'blue', e.OSCHI12),
			new Data('PDB-REDO', s.map(d => d.FCHI12), e.FCHI12, 'orange', e.FSCHI12)
		];

		const rRange = [...dataRama, ...dataRota].map(d => d.range)
			.reduce((a, b) => [Math.min(a[0], b[0]), Math.max(a[1], b[1])]);

		td.appendChild(
			BoxPlot(225, 500, "Ramachandran Plot", "Z-score", dataRama, rRange));

		td.appendChild(
			BoxPlot(225, 500, "Rotamer quality", "Z-score", dataRota, rRange));
	}
}

export function createBoxPlot(data, td, url) {
	const ureso = data.URESO;
	fetch(`${url}/gfx/statistics-for-box-plot?ureso=${ureso}`)
		.then(r => r.json())
		.then(s => createBoxPlots(data, s, td))
		.catch(e => console.log(e));
}

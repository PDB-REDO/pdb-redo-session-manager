// Import the LitElement base class and html helper function
import { LitElement, html, css } from 'lit';
import * as d3 from 'd3';

const BackgroundTypes = Object.freeze({
	"contour": { name: "Contour", nr: 2, img: "line", alhpa: 1 },
	"heatmap": { name: "Heatmap", nr: 1, img: "heatmap", alhpa: 0.6 }
});
const RamaSubset = Object.freeze({
	"general": { name: "General case", nr: 1, img: 'GeneralContour' },
	"ileval": { name: "Isoleucine and valine", nr: 2, img: 'IleVal' },
	"prepro": { name: "Pre-proline", nr: 3, img: 'PrePro' },
	"gly": { name: "Glycine", nr: 4, img: 'Gly' },
	"transpro": { name: "Trans proline", nr: 5, img: 'TransPro' },
	"cispro": { name: "Cis proline", nr: 6, img: 'CisPro' }
});

const radius = 3;

// Extend the LitElement base class
export class RamachandranPlot extends LitElement {

	static get properties() {
		return {
			width: { type: Number },
			error: { type: String },
			noshadow: { type: Boolean, attribute: 'no-shadow'},
			noheader: { type: Boolean, attribute: 'no-header'}
		}
	}

	constructor() {
		super();

		// Some default values:

		this.width = 400;
		this.noshadow = false;
		this.noheader = false;
        this.error = null;
        
		this.pdbRedoBaseURI = document.querySelector('script[src$="pdb-redo-result-loader.js"]')
			.src
			.replace(/scripts\/pdb-redo-result-loader\.js$/, '');

		this.hoveredItem = '';
		this.hoveredData = null;

		this.margin = {top: 30, right: 30, bottom: 30, left: 30};

		this.backgroundType = BackgroundTypes.contour;

		this.images = {};
		this.loadImages(BackgroundTypes.contour);

		this.show = {
			preferred: false,
			allowed: true,
			outlier: true
		};
	}

	static get styles() {
		return css`
			* {
				font-family: "Verdana", "Arial", "Helvetica", "sans-serif";
			}

			header, footer {
				color: white;
				background-color: #3366cc;
			}

			header img {
				background-color: white;
				height: 1.2em;
				width: auto;
			}

			g.tick line {
				stroke: lightgrey;
				opacity: 0.7;
				shape-rendering: crispEdges;
			}

			g.res circle {
				fill: #2c58b0;
				opacity: 0.6;
			}

			circle.orig-label {
				fill: #2c58b0;
				opacity: 0.6;
			}

			g.res {
				display: none;
			}

			svg.show-preferred g.res.preferred {
				display: unset;
			}

			svg.show-allowed g.res.allowed {
				display: unset;
			}

			svg.show-outlier g.res.outlier {
				display: unset;
			}

			g.res circle.redo, circle.redo-label {
				fill: #FF9933;
				opacity: 0.6;
			}

			g.res line {
				stroke: gray;
				stroke-width: 1;
				opacity: 0.6;
			}

			g.res.hover circle, g.res.hover line {
				opacity: 1;
			}

			g.res.hover circle {
				stroke: red;
				stroke-width: 0.75;
			}

			g.res.hover line {
				stroke: red;
			}

			svg, canvas {
				position: absolute;
			}

			canvas {
				z-index: -1;
			}

			footer {
				margin-top: 1rem;
			}

			footer p {
				font-size: small;
			}
		`;
	}

	render() {
		return html`
			<link rel="stylesheet" href="${new URL('css/w3.css', this.pdbRedoBaseURI)}">
			<!-- template content -->
			<div class="${this.noshadow ? '' : 'w3-card'}" style="width: ${this.width + 32}px">
				${this.noheader ? '' : html`
					<header class="w3-container">
						<h3><a href="https://pdb-redo.eu">
							<img src="${new URL('images/PDB_logo_rect_small.svg', this.pdbRedoBaseURI)}" alt="PDB-REDO"/></a>
							Kleywegt-like plot for ${this.pdbID}
						</h3>
					</header>`
				}

				<div class="w3-container">
					${this.error !== null && this.error != 'null' ?
						html`
							<div class="w3-panel w3-pale-red w3-border">
								<h3>Error</h3>
								<p>${this.error}</p>
							</div>
						`:''}

					<div style="width: ${this.width}px; height: ${this.width}px"
						@click="${() => this.toggleBackground()}">
						<svg width="${this.width}" height="${this.width}"
							preserveAspectRatio='xMinYMin meet'
							viewBox='0 0 ${this.width} ${this.width}'
							class="${this.show['preferred'] ? 'show-preferred' : ''}
								   ${this.show['allowed'] ? 'show-allowed' : ''}
								   ${this.show['outlier'] ? 'show-outlier' : ''}">
						</svg>

						<canvas width="${this.width}" height="${this.width}"></canvas>
					</div>

					<table class="w3-table-all ${this.noshadow ? '' : 'w3-card'} w3-small w3-margin-bottom">
						<tr>
							<th colspan="2">Description</th>
							<th>PDB
								<svg width='20' height='16' viewBox='0 0 20 16'>
									<circle class="orig-label" cx='12' cy='8' r='6'/>
								</svg>
							</th>
							<th>PDB-REDO
								<svg width='20' height='16' viewBox='0 0 20 16'>
									<circle class="redo-label" cx='12' cy='8' r='6'/>
								</svg>
							</th>
						</tr>
						<tr>
							<td colspan="2">${this.hoveredItem ? this.hoveredItem.name : '\u00a0'}</td>
							<td>${this.hoveredItem ? this.hoveredItem.orig : '\u00a0'}</td>
							<td>${this.hoveredItem ? this.hoveredItem.redo : '\u00a0'}</td>
						</tr>
						<tr>
							<td colspan="2">
								Ramachandran Z-score
							</td>
							<td>${this.stats ? this.stats.orig.zscore : '\u00a0'}</td>
							<td>${this.stats ? this.stats.redo.zscore : '\u00a0'}</td>
						</tr>
						<tr>
							<td>
								<img src="${new URL(this.show['preferred'] ? 'images/checkbox-checked.svg' : 'images/checkbox-unchecked.svg', this.pdbRedoBaseURI)}"
									 data-target="preferred" @click="${this.toggleChecbox}"/>
							</td>
							<td>Preferred regions</td>
							<td>${this.stats ? this.stats.orig.preferred : '\u00a0'}</td>
							<td>${this.stats ? this.stats.redo.preferred : '\u00a0'}</td>
						</tr>
						<tr>
							<td>
								<img src="${new URL(this.show['allowed'] ? 'images/checkbox-checked.svg' : 'images/checkbox-unchecked.svg', this.pdbRedoBaseURI)}"
									data-target="allowed" @click="${this.toggleChecbox}"/>
							</td>
							<td>Allowed regions</td>
								<td>${this.stats ? this.stats.orig.allowed : '\u00a0'}</td>
							<td>${this.stats ? this.stats.redo.allowed : '\u00a0'}</td>
						</tr>
						<tr>
							<td>
								<img src="${new URL(this.show['outlier'] ? 'images/checkbox-checked.svg' : 'images/checkbox-unchecked.svg', this.pdbRedoBaseURI)}"
									data-target="outlier" @click="${this.toggleChecbox}"/>
							</td>
							<td>Outliers</td>
								<td>${this.stats ? this.stats.orig.outliers : '\u00a0'}</td>
							<td>${this.stats ? this.stats.redo.outliers : '\u00a0'}</td>
						</tr>
					</table>
				</div>
			</div>
			`;
	}

	updated(changedProperties) {
		let needRebuildSVG = d3.select(this.shadowRoot.querySelector('svg')).select('g').empty();
		let needReloadPDB = this.data == null && this.getAttribute('error') == '';

		changedProperties.forEach((oldValue, propName) => {
			switch (propName)
			{
				case 'width':
					needRebuildSVG = true;
					break;
			}
		});

		if (needRebuildSVG)
		{
			this.rebuildSVG();
			if (needReloadPDB == false && this.data != null)
				this.updateGraph();
		}

		if (needReloadPDB)
		{
			this.removeAttribute('error');

			// Promise.all(['orig','redo'].map(type => 
			// 			if (r.ok)
			// 				return r.json();
			// 			this.setAttribute('error', `error fetching orig data for ${this.pdbID} : ${r.statusText}`);
			// 		})
			// 	)).then(data => {
			// 		const [orig, origZscore] = [data[0][this.pdbID].molecules, data[0][this.pdbID].zscore];
			// 		const [redo, redoZscore] = [data[1][this.pdbID].molecules, data[1][this.pdbID].zscore];
			// 		this.makeData(orig, redo, origZscore, redoZscore);
			// 		this.updateGraph();
			// 	})
			// 	.catch(err => {
			// 		console.log(err);
			// 		this.setAttribute('error', `error fetching data for ${this.pdbID}`)
			// 	});
		}
	}

	// Recreate the basic SVG (axes and such), called when the width changes
	rebuildSVG() {
		const svg = d3.select(this.shadowRoot.querySelector('svg'));

		const margin = this.margin;

		const width = this.width - margin.left - margin.right;
		const height = width;

		svg.selectAll('.graph-g').remove();

		const g = svg.append("g")
			.classed("graph-g", true)
			.attr("transform", `translate(${margin.left},${margin.top})`);
		
		const xScale = d3.scaleLinear()
			.domain([-180, 180])
			.range([0, width]);
		const xAxisBottom = d3.axisBottom(xScale)
			.tickSize(-height);
		g.append("g")
			.classed("x", true)
			.attr("transform", `translate(0, ${height})`)
			.call(xAxisBottom);
		const xAxisTop = d3.axisTop(xScale)
			.tickSize(0);
		g.append("g")
			.classed("x", true)
			.call(xAxisTop);

		const yScale = d3.scaleLinear()
			.domain([180, -180])
			.range([0, height]);
		
		const yAxisRight = d3.axisRight(yScale)
			.tickSize(-width);
		g.append("g")
			.classed("y", true)
			.attr("transform", `translate(${width}, 0)`)
			.call(yAxisRight);
		const yAxisLeft = d3.axisLeft(yScale)
			.tickSize(0);
		g.append("g")
			.classed("y", true)
			.call(yAxisLeft);
		
		g.append("text")
			.attr("class", "x axis-label")
			.attr("text-anchor", "middle")
			.attr("x", width / 2)
			.attr("y", height + margin.bottom - 6)
			.text('\u03C6');

		g.append("text")
			.attr("class", "y axis-label")
			.attr("text-anchor", "middle")
			.attr("x", 6 - this.margin.left)
			.attr("y", height / 2)
			.text('\u03C8');

		this.updateBackground();
	}

	// routine to create precalculated data array
	setData(data, rama) {

		this.stats = {
			orig: {
				zscore: data.OZRAMA,
				preferred: 0,
				allowed: 0,
				outliers: 0
			},
			redo: {
				zscore: data.FZRAMA,
				preferred: 0,
				allowed: 0,
				outliers: 0
			}
		}

		// The actual data
		const dataMap = new Map();

		rama.forEach(entity => {
			entity.chains.forEach(chain => {
				chain.models[0].residues.forEach(residue => {
					const key = `${chain.asym_id}-${residue.seq_id}`;
					dataMap.set(key, {
						chain: chain.asym_id,
						residue_name: residue.compound_id,
						residue_number: residue.seq_id,
						key: key,
						orig: {
							phi: residue.orig.phi,
							psi: residue.orig.psi,
							rama: residue.orig.rama,
							cis_peptide: residue.orig.cis
						},
						redo: {
							phi: residue.redo.phi,
							psi: residue.redo.psi,
							rama: residue.redo.rama,
							cis_peptide: residue.redo.cis
						}
					});

					for (let s of ['orig', 'redo']) {
						switch (residue[s].rama)
						{
							case 'Favored': this.stats[s].preferred += 1; break;
							case 'Allowed': this.stats[s].allowed += 1; break;
							case 'OUTLIER': this.stats[s].outliers += 1; break;
						}
					}
				})
			})
		});

		this.data = [...dataMap.values()];

		const width = this.width - this.margin.left - this.margin.right;
		const height = width;

		const xScale = d3.scaleLinear()
			.domain([-180, 180])
			.range([0, width]);

		const yScale = d3.scaleLinear()
			.domain([180, -180])
			.range([0, height]);

		this.data.forEach(d => {
			const xo = xScale(d.orig.phi);
			const yo = yScale(d.orig.psi);
			const xr = xScale(d.redo.phi);
			const yr = yScale(d.redo.psi);
			d.dist = Math.sqrt((xo - xr) * (xo - xr) + (yo - yr) * (yo - yr));

			const xm = [(xo + xr) / 2, (xo + xr) / 2];
			const ym = [(yo + yr) / 2, (yo + yr) / 2];

			if (Math.abs(d.orig.phi - d.redo.phi) > 180)
			{
				if (d.orig.phi < d.redo.phi)
				{
					xm[0] = 0;
					xm[1] = width;

					const f = xo / (width - xr + xo);
					ym[0] = ym[1] = yo + f * (yr - yo);
				}
				else
				{
					xm[0] = width;
					xm[1] = 0;

					const f = xr / (width - xo + xr);
					ym[0] = ym[1] = yr + f * (yo - yr);
				}
			}
			else if (Math.abs(d.orig.psi - d.redo.psi) > 180)
			{
				if (d.orig.psi > d.redo.psi)
				{
					ym[0] = 0;
					ym[1] = height;

					const f = yo / (width - yr + yo);
					xm[0] = xm[1] = xo + f * (xr - xo);
				}
				else
				{
					ym[0] = height;
					ym[1] = 0;

					const f = yr / (width - yo + yr);
					xm[0] = xm[1] = xr + f * (xo - xr);
				}
			}

			d.lines = {
				orig: { x1: xo, x2: xm[0], y1: yo, y2: ym[0] },
				redo: { x1: xr, x2: xm[1], y1: yr, y2: ym[1] }
			};
		});

		// finally assign subclass to the data items

		for (let i = 1; i + 1 < this.data.length; ++i)
		{
			switch (this.data[i].residue_name)
			{
				case 'GLY': this.data[i].subclass = 'gly'; break;
				case 'ILE':
				case 'VAL':	this.data[i].subclass = 'ileval'; break;
				case 'PRO': {
					if (this.data[i - 1]['redo'].cis_peptide)
						this.data[i].subclass = 'cispro';
					else
						this.data[i].subclass = 'transpro';
					break;
				}
				default:
					if (this.data[i + 1].residue_name == 'PRO')
						this.data[i].subclass = 'prepro';
					else
						this.data[i].subclass = 'general';
			}
		}

		this.requestUpdate();
	}

	updateGraph() {
		const margin = this.margin;
		const width = this.width - margin.left - margin.right;
		const height = width;

		const svg = d3.select(this.shadowRoot.querySelector('svg'));
		const g = svg.select('.graph-g');

		const xScale = d3.scaleLinear()
			.domain([-180, 180])
			.range([0, width]);

		const yScale = d3.scaleLinear()
			.domain([180, -180])
			.range([0, height]);

		const gs = g.selectAll("g.res")
			.data(this.data.filter(d => typeof d.orig !== 'undefined' && typeof d.redo !== 'undefined' && d.orig.phi !== null), d => d.key);
		
		gs.exit().remove();
		const ggs = gs.enter()
			.append("g")
			.classed("res", true)
			.classed("preferred",	d => d['orig'].rama === 'Favored' || d['redo'].rama === 'Favored')
			.classed("allowed",		d => d['orig'].rama === 'Allowed' || d['redo'].rama === 'Allowed')
			.classed("outlier",		d => d['orig'].rama === 'OUTLIER' || d['redo'].rama === 'OUTLIER');

		['orig', 'redo'].forEach(t => {

			ggs.append("circle")
				.classed("dot", true)
				.classed(t, true)
				.attr("r", radius)
				.on("mouseover", (e, d) => this.onMouseOver(e.target, d))
				.on("mouseout", (e, d) => this.onMouseOut(e.target, d));

			ggs.merge(gs)
				.select(`circle.${t}`)
				.attr("cx", d => xScale(d[t].phi))
				.attr("cy", d => yScale(d[t].psi));

			ggs.append("line")
				.classed("change", true)
				.classed(`${t}-part`, true)
				.on("mouseover", (e, d) => this.onMouseOver(e.target, d))
				.on("mouseout", (e, d) => this.onMouseOut(e.target, d));
	
			ggs.merge(gs)
				.select(`line.change.${t}-part`)
				.attr("x1", d => d.lines[t].x1)
				.attr("x2", d => d.lines[t].x2)
				.attr("y1", d => d.lines[t].y1)
				.attr("y2", d => d.lines[t].y2);
		});
		
		this.updateBackground();
	}

	// background images
	loadImages(bg) {
		const margin = this.margin;
		const width = this.width - margin.left - margin.right;
		const height = width;

		Object.keys(RamaSubset).forEach(id => {
			const img = new Image(width, height);

			this.images[id] = {
				img: img,
				img_loaded: false
			};

			img.onload = () => this.images[id].img_loaded = true;

			img.src = new URL(`images/${bg.img + RamaSubset[id].img}.png`, this.pdbRedoBaseURI);
		});
	}

	toggleBackground() {
		if (this.background === BackgroundTypes.heatmap)
			this.background = BackgroundTypes.contour;
		else
			this.background = BackgroundTypes.heatmap;

		this.loadImages(this.background);
		this.updateBackground();
	}

	updateBackground() {
		const select = this.hoveredData ? this.hoveredData.subclass : 'general';
		const img = this.images[select];

		if (this.selectBackground != img)
		{
			this.selectBackground = img;

			const margin = this.margin;
			const width = this.width - margin.left - margin.right;
			const height = width;

			// const imgBackgroundType = BackgroundTypes.contour.img;
			const imgBackgroundAlpha = BackgroundTypes.contour.alhpa;
	
			const context = this.shadowRoot.querySelector('canvas').getContext('2d');
			context.globalAlpha = imgBackgroundAlpha;
	
			context.clearRect(margin.left + 1, margin.top + 1, width - 2, height - 2);

			if (img.img_loaded)
				context.drawImage(img.img, margin.left + 1, margin.top + 1, width - 2, height - 2);
			else
				img.img.onload = () => {
					this.images[select].img_loaded = true;
					context.drawImage(img.img, margin.left + 1, margin.top + 1, width - 2, height - 2);
				};
		}
	}

	toggleChecbox(event) {
		const checkbox = event.target;
		const target = checkbox.dataset.target;
		const svg = this.shadowRoot.querySelector('svg');

		if (this.show[target])
		{
			this.show[target] = false;
			svg.classList.remove(`show-${target}`);
			checkbox.src = new URL('images/checkbox-unchecked.svg', this.pdbRedoBaseURI);
		}
		else
		{
			this.show[target] = true;
			svg.classList.add(`show-${target}`)
			checkbox.src = new URL('images/checkbox-checked.svg', this.pdbRedoBaseURI);
		}
	}

	onMouseOver(n, d) {
		n.parentNode.classList.add("hover");
		
		this.hoveredItem = {
			name: `${d.chain}:${d.residue_number} ${d.residue_name} / ${RamaSubset[d.subclass].name}`,
			orig: `${d.orig.phi.toFixed(1)}/${d.orig.psi.toFixed(1)}`,
			redo: `${d.redo.phi.toFixed(1)}/${d.redo.psi.toFixed(1)}`
		};
		
		this.requestUpdate();

		this.hoveredData = d;
		this.updateBackground();
	}

	onMouseOut(n, d) {
		n.parentNode.classList.remove("hover");
		this.hoveredItem = null;
		this.requestUpdate();

		this.hoveredData = null;
		this.updateBackground();
	}

}

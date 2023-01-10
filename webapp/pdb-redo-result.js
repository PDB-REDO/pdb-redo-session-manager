// Import the LitElement base class and html helper function
// import { LitElement, html, css } from 'lit';
// import * as d3 from 'd3';
import { PDBRedoApiRequest } from './request';
import pdb_redo_style from './pdb-redo-result.scss';

// Extend the LitElement base class
class PDBRedoResult extends HTMLElement {

	// static get properties() {
	// 	return {
	// 		tokenID: { type: String, attribute: 'token-id' },
	// 		tokenSecret: { type: String, attribute: 'token-secret' },
	// 		jobID: { type: String, attribute: 'job-id' },
	// 		error: { type: String },
	// 		link: { type: String, attribute: 'url' }
	// 	}
	// }

	constructor() {
		super();

		// problem in web components for now is that you cannot set
		// the base for relative URL's. That's why we hack around
		// this limitation here. Assume the loader script is not renamed.
		this.pdbRedoBaseURI = document.querySelector('script[src$="pdb-redo-result-loader.js"]')
			.src
			.replace(/scripts\/pdb-redo-result-loader\.js$/, '');

		// Some default values:

		this.data = null;
		this.error = null;
		this.link = 'https://services.pdb-redo.eu';

		// the shadow context
		const shadow = this.attachShadow({ mode: 'open' });

		const div = document.createElement('div');
		const style = document.createElement('style');
		shadow.appendChild(style);
		shadow.appendChild(div);

		style.innerHTML = pdb_redo_style;
	}

	loadData(req) {
		let statusOK = false;
		return fetch(req)
			.then(response => {
				statusOK = response.ok;
				return response.json()
			})
			.then(data => {
				if (!statusOK)
					this.setAttribute('error', `error fetching data for ${this.jobID} : ${data.error}`);
				else {
					return data;
				}
			});
	}

	connectedCallback() {
		const url = this.getAttribute('url');
		const tokenID = this.getAttribute('token-id');
		const tokenSecret = this.getAttribute('token-secret');
		const jobID = this.getAttribute('job-id');

		const shadow = this.shadowRoot;
		// shadow.querySelector('style').textContent = `
		// 	@import(url ${url}/css/style.css);
		// `;

		this.loadData(new PDBRedoApiRequest(`${url}/api/session/${tokenID}/run/${jobID}/output/data.json`, {
			token: {
				id: tokenID,
				secret: tokenSecret,
			}
		})).then(data => {
			const fd = new FormData();
			fd.append('data.json', JSON.stringify(data));
	
			fetch(`${url}/entry`, {
				method: "POST",
				body: fd
			}).then(r => {
				if (r.ok)
					return r.text();
			}).then(r => {
				shadow.querySelector('div').innerHTML = r;
			});
		});

	}

	// render() {
	// 	return html`
	// 		<link rel="stylesheet" href="${new URL('css/w3.css', this.pdbRedoBaseURI)}">
	// 		<!-- template content -->
	// 		<p>${this.data ? this.data.TITLE : 'no title yet'}</p>
	// 		`;
	// }

	// updated(changedProperties) {
	// 	// let needRebuildSVG = d3.select(this.shadowRoot.querySelector('svg')).select('g').empty();
	// 	let needReloadData = this.data == null && this.getAttribute('error') == '';

	// 	changedProperties.forEach((oldValue, propName) => {
	// 		switch (propName) {
	// 			case 'width':
	// 				needRebuildSVG = true;
	// 				break;

	// 			case 'jobID':
	// 				if (this.jobID !== null)
	// 					needReloadData = true;
	// 				break;

	// 			case 'url':
	// 				this.link = this.link.replace(/\/+$/, '');
	// 				break;
	// 		}
	// 	});

	// 	// if (needRebuildSVG)
	// 	// {
	// 	// 	this.rebuildSVG();
	// 	// 	if (! needReloadData)
	// 	// 		this.updateGraph();
	// 	// }

	// 	if (needReloadData) {
	// 		this.removeAttribute('error');

	// 		const req = new PDBRedoApiRequest(`${this.link}/api/session/${this.tokenID}/run/${this.jobID}/output/data.json`, {
	// 			token: {
	// 				id: this.tokenID,
	// 				secret: this.tokenSecret,
	// 			}
	// 		});

	// 		let statusOK = false;
	// 		fetch(req)
	// 			.then(response => {
	// 				statusOK = response.ok;
	// 				return response.json()
	// 			})
	// 			.then(data => {
	// 				if (!statusOK)
	// 					this.setAttribute('error', `error fetching data for ${this.jobID} : ${data.error}`);
	// 				else {
	// 					this.data = data;
	// 					this.requestUpdate();
	// 				}
	// 			}).catch(err => {
	// 				console.log(err);
	// 				this.setAttribute('error', `error fetching data for ${this.pdbID}`)
	// 			});
	// 	}
	// }



}
// Register the new element with the browser.
customElements.define('pdb-redo-result', PDBRedoResult);


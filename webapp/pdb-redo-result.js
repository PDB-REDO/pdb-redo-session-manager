import { PDBRedoRequest } from './PDBRedoRequest';
import { createBoxPlot } from "./boxplot";
import { RamachandranPlot } from './ramaplot';

// Extend the LitElement base class
class PDBRedoResult extends HTMLElement {

	constructor() {
		super();

		// problem in web components for now is that you cannot set
		// the base for relative URL's. That's why we hack around
		// this limitation here. Assume the loader script is not renamed.
		this.pdbRedoBaseURI = document.querySelector('script[src$="pdb-redo-result-loader.js"]')
			.src
			.replace(/\/?scripts\/pdb-redo-result-loader\.js$/, '');

		// Some default values:

		this.data = null;
		this.error = null;
		this.url = 'https://services.pdb-redo.eu';

		// the shadow context
		const shadow = this.attachShadow({ mode: 'open' });

		const link = document.createElement('link');
		link.setAttribute('href', `${this.pdbRedoBaseURI}/css/web-component-style.css`);
		link.setAttribute('rel', 'stylesheet');
		shadow.appendChild(link);

		const div = document.createElement('div');
		// const style = document.createElement('style');
		// shadow.appendChild(style);
		shadow.appendChild(div);

		// style.innerHTML = pdb_redo_style;
	}

	connectedCallback() {
		const url = this.getAttribute('url');
		if (url != null)
			this.url = url.replace(/\/+$/, '');	// strip trailing slashes

		this.pdbID = this.getAttribute('pdb-id');

		this.tokenID = this.getAttribute('token-id');
		this.tokenSecret = this.getAttribute('token-secret');
		this.jobID = this.getAttribute('job-id');

		if (this.url != null) {
			if (this.pdbID != null)
				this.reloadDBData();
			else if (this.jobID != null && (this.tokenID == null || this.tokenSecret == null))
				this.reloadLocalJobData();
			else if (this.tokenID != null && this.tokenSecret != null && this.jobID != null)
				this.reloadJobData();
		}
	}

	displayError(err) {
		const msg = `${err}`;

		const shadow = this.shadowRoot;
		const div = shadow.querySelector('div');
		shadow.removeChild(div);

		const alert = document.createElement('div');
		alert.classList.add('alert', 'alert-warning');
		alert.setAttribute('role', 'alert');

		const h3 = document.createElement('h3');
		h3.innerText = 'Something went wrong rendering the requested data';
		alert.appendChild(h3);

		const mdiv = document.createElement('div');
		mdiv.innerText = `The error message was: ${msg}`;
		alert.appendChild(mdiv);

		shadow.appendChild(alert);

		const style = shadow.querySelector('style');
		style.innerHTML = pdb_redo_style;
	}

	attributeChangedCallback(name, oldValue, newValue) {

		let needReload = false;
		switch (name) {
			case 'url':
				if (this.url !== newValue) {
					this.url = newValue;
					needReload = true;
				}
				break;

			case 'pdb-id':
				if (this.pdbID !== newValue) {
					this.pdbID = newValue;
					needReload = true;
				}
				break;
		}

		if (needReload && this.url != null) {
			if (this.pdbID != null)
				this.reloadDBData();
			else
				this.reloadJobData();
		}
	}

	reloadDBData() {
		fetch(`${this.url}/db/entry?pdb-id=${this.pdbID}`, { method: "post" })
			.then(r => {
				if (r.ok)
					return r.text();
				else
					throw `Error fetching data, status code was ${r.status}`;
			}).then(r => this.putEntry(r))
			.catch(err => {
				this.displayError(err);
			});
	}

	reloadLocalJobData() {
		fetch(`${this.url}/job/entry/${this.jobID}`, { credentials: 'include' })
			.then(r => {
				if (r.ok)
					return r.text();
				else
					throw `Error fetching data, status code was ${r.status}`;
			}).then(r => this.putEntry(r))
			.catch(err => {
				this.displayError(err);
			});
	}

	reloadJobData() {
		fetch(new PDBRedoRequest(`${this.url}/api/run/${this.jobID}/output/data.json`, {
			token: {
				id: this.tokenID,
				secret: this.tokenSecret,
			}
		})).then(r => {
			if (r.ok)
				return r.json();
			else
				throw `Error fetching data, status code was ${r.status}`;
		}).then(data => {
			const fd = new FormData();
			fd.append('data.json', JSON.stringify(data));
			fd.append('link-url', `${this.url}/job/${this.jobID}/output/`);

			fetch(`${this.url}/entry`, {
				method: "POST",
				body: fd
			}).then(r => {
				if (r.ok)
					return r.text();
				else
					throw `Error fetching data, status code was ${r.status}`;
			}).then(r => this.putEntry(r));
		})
			.catch(err => {
				this.displayError(err);
			});
	}

	putEntry(r) {
		const shadow = this.shadowRoot;

		shadow.querySelector('div').innerHTML = r;

		/* Code to manage a the toggle between raw and percentile values */
		Array.from(shadow.querySelectorAll("button.toggle"))
			.forEach(p => p.addEventListener('click', (e) => {
				const state = e.target.dataset.state;
				const raw = state === "raw";
				Array.from(shadow.querySelectorAll("table.perc-raw-toggle"))
					.forEach(e => {
						e.classList.toggle('perc', raw === false);
						e.classList.toggle('raw', raw === true);
					});
			})
			);

		const entryDataJSON = shadow.querySelector("#entry-data").innerHTML;

		const entryData = JSON.parse(entryDataJSON);

		const boxPlotTD = shadow.querySelector("#boxPlotTD");
		if (boxPlotTD != null && entryData != null) {
			const data = entryData.data;

			if (data.RFREE == null || data.ZCALERR === true || data.TSTCNT !== data.NTSTCNT)
				data.RFREE = data.RFCALUNB;
			else
				this.RFREE = data.RFCAL;
			createBoxPlot(data, boxPlotTD, this.url);
		}

		const ramaPlot = shadow.querySelector('ramachandran-plot');
		if (typeof entryData['rama-angles'] === "object" && entryData['rama-angles'] != null)
			ramaPlot.setData(entryData.data, entryData['rama-angles']);
		else {
			const ramaPlotDiv = shadow.querySelector('#rama-plot-div');
			ramaPlotDiv.classList.add('hide');
		}
	}
}

customElements.define('pdb-redo-result', PDBRedoResult);
customElements.define('ramachandran-plot', RamachandranPlot);

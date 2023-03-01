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

		this.pdb_redo_url = 'https://pdb-redo.eu';
		this.data_url = null;
		this.include_credentials = false;

		// the shadow context
		const shadow = this.attachShadow({ mode: 'open' });

		const link = document.createElement('link');
		link.setAttribute('href', `${this.pdbRedoBaseURI}/css/web-component-style.css`);
		link.setAttribute('rel', 'stylesheet');
		link.setAttribute('crossorigin', 'anonymous');
		shadow.appendChild(link);

		const div = document.createElement('div');
		// const style = document.createElement('style');
		// shadow.appendChild(style);
		shadow.appendChild(div);
	}

	connectedCallback() {
		const data_url = this.getAttribute('data-url');
		if (data_url != null)
			this.data_url = data_url.replace(/\/+$/, '');	// strip trailing slashes
		
		const pdb_url = this.getAttribute('pdb-redo-url');
		if (pdb_url != null)
			this.pdb_redo_url = pdb_url.replace(/\/+$/, '');	// strip trailing slashes

		this.include_credentials = this.getAttribute('include-credentials') != null;

		this.pdbID = this.getAttribute('pdb-id');

		this.tokenID = this.getAttribute('token-id');
		this.tokenSecret = this.getAttribute('token-secret');
		this.jobID = this.getAttribute('job-id');

		this.reload();
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
	}

	attributeChangedCallback(name, oldValue, newValue) {

		let needReload = false;
		switch (name) {
			case 'data-url':
				if (this.data_url !== newValue) {
					this.data_url = newValue;
					needReload = true;
				}
				break;

			case 'pdb-redo-url':
				if (this.data_url !== newValue) {
					this.data_url = newValue;
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

		if (needReload)
			this.reload();
	}

	reload() {
		if (this.pdbID != null)
			this.reloadDBData();
		else if (this.data_url != null)
			this.reloadRemoteData();
	}

	reloadDBData() {
		fetch(`${this.pdb_redo_url}/db/entry?pdb-id=${this.pdbID}`, { method: "post" })
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

	reloadRemoteData() {
		const options = {};
		if (this.include_credentials)
			options.credentials = 'include';

		fetch(`${this.data_url}/data.json`, options)
			.then(r => {
				if (r.ok)
					return r.json();
				else
					throw `Error fetching data, status code was ${r.status}`;
			}).then(data => {
				const fd = new FormData();
				fd.append('data.json', JSON.stringify(data));
				fd.append('link-url', this.data_url);

				fetch(`${this.pdb_redo_url}/entry`, {
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
			createBoxPlot(data, boxPlotTD, this.pdb_redo_url);
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

import { PDBRedoApiRequest } from './request' ;


class ApiTester {
	constructor() {
		document.forms["token-form"].addEventListener("submit", (evt) => this.createToken(evt));
		document.forms["delete-token-form"].addEventListener("submit", (evt) => this.deleteToken(evt));
		document.getElementById("fetch-runs-btn").addEventListener('click', (evt) => this.fetchRuns(evt));

		this.token = null;
	}

	createToken(evt) {
		evt.preventDefault();
  
		const form = document.forms["token-form"];
		const data = new FormData(form);

		fetch(form.action, {
			method: "POST",
			body: data
		}).then(response => {
			if (response.ok)
				return response.json();
			throw response;
		}).then(token => {
			this.token = token;
			const tokenForm = document.forms["delete-token-form"];
			tokenForm["token-id"].value = token.id;
			tokenForm["token-name"].value = token.name;
			tokenForm["token-secret"].value = token.token;
			tokenForm["token-expires"].value = new Date(token.expires);

		}).catch(err => {
			console.log(err);
			alert("Failed to get token " + err);
		});
	}

	deleteToken(evt) {
		evt.preventDefault();

		if (typeof this.token === 'undefined')
		{
			alert("Please create a token first");
			return;
		}

		const req = new PDBRedoApiRequest(`/ajax/session/${this.token.id}`, {
			method: "DELETE",
			token: {
				id: this.token.id,
				secret: this.token.token
			}
		});

		fetch(req).then(response => {
			if (response.ok)
			{
				const tokenForm = document.forms["delete-token-form"];
				tokenForm["token-id"].value = "";
				tokenForm["token-name"].value = "";
				tokenForm["token-secret"].value = "";
				tokenForm["token-expires"].value = null;				
			}
			else
				return response.json();
		}).then(err => {
			if (typeof err === 'object' && err.error)
				throw err.error;
		}).catch(err => {
			console.log(err);
			alert("Failed to get token " + err);
		});
	}

	fetchRuns(e) {
		if (e) e.preventDefault();

		if (typeof this.token === 'undefined')
		{
			alert("Please create a token first");
			return;
		}

		const req = new PDBRedoApiRequest(`/ajax/session/${this.token.id}/run`, {
			token: {
				id: this.token.id,
				secret: this.token.token
			}
		});

		fetch(req)
		.then(response => response.json())
		.then(data => {
			if (typeof data === 'object' && data.error)
				throw data.error;
			
			const tbody = document.querySelector("#run-table tbody");
			[...tbody.querySelectorAll("tr")].forEach(tr => tbody.removeChild(tr));

			let i = 0;
			for (let run of data) {
				const row = document.createElement("tr");
				const td1 = document.createElement("td");
				td1.textContent = `${++i}`;
				row.appendChild(td1);
				const td2 = document.createElement("td");
				td2.textContent = run.name;
				row.appendChild(td2);
				tbody.appendChild(row);
			}

		}).catch(err => {
			console.log(err);
			alert("Failed to list runs: " + err);
		});
		

	}

}

window.addEventListener('load', () => new ApiTester());
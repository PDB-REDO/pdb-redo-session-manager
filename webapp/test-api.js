import { PDBRedoApiRequest } from './request' ;
import 'bootstrap';

class ApiTester {
	constructor() {
		document.forms["token-form"].addEventListener("submit", (evt) => this.createToken(evt));
		document.forms["delete-token-form"].addEventListener("submit", (evt) => this.deleteToken(evt));
		document.getElementById("fetch-runs-btn").addEventListener('click', (evt) => this.fetchRuns(evt));

		document.forms["fetch-token-form"].addEventListener('submit', (evt) => this.fetchToken(evt));
		document.forms["submit-job-form"].addEventListener('submit', (evt) => this.submitJob(evt));

		this.token = null;
	}

	createToken(evt) {
		evt.preventDefault();
  
		const form = document.forms["token-form"];
		const data = new FormData(form);

		let statusOK;
		fetch(form.action, {
			method: "POST",
			body: data
		}).then(response => {
			statusOK = response.ok;
			return response.json();
		}).then(token => {
			if (statusOK) {
				this.token = token;
				const tokenForm = document.forms["delete-token-form"];
				tokenForm["token-id"].value = token.id;
				tokenForm["token-name"].value = token.name;
				tokenForm["token-secret"].value = token.token;
				tokenForm["token-expires"].value = new Date(token.expires);
			}
			else {
				throw token.error;
			}
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

		const req = new PDBRedoApiRequest(`/api/session/${this.token.id}`, {
			method: "DELETE",
			token: {
				id: this.token.id,
				secret: this.token.token
			}
		});

		let statusOK;
		fetch(req).then(response => {
			statusOK = response.ok;
			return response.json();
		}).then(data => {
			if (statusOK)
			{
				const tokenForm = document.forms["delete-token-form"];
				tokenForm["token-id"].value = "";
				tokenForm["token-name"].value = "";
				tokenForm["token-secret"].value = "";
				tokenForm["token-expires"].value = null;				
			}
			else
				throw data.error;
		}).catch(err => {
			console.log(err);
			alert("Failed to get token " + err);
		});
	}

	fetchToken(evt) {
		evt.preventDefault();

		const form = document.forms["fetch-token-form"];
		const tokenID = form["token-id"].value;
		
		const req = new PDBRedoApiRequest(`/api/session/${tokenID}`, {
			method: "GET",
			token: {
				id: tokenID,
				secret: form["token-secret"].value
			}
		});

		let statusOK;
		fetch(req).then(response => {
			statusOK = response.ok;
			return response.json();
		}).then(token => {
			if (statusOK)
			{
				const tokenForm = document.forms["fetch-token-result"];
				tokenForm["token-name"].value = token.name;
				tokenForm["token-expires"].value = token.expires;
			}
			else
				throw data.error;
		}).catch(err => {
			console.log(err);
			alert("Failed to get token " + err);
		});
	}

	submitJob(e) {
		e.preventDefault();

		const form = document.forms["submit-job-form"];
		const tokenID = form["token-id"].value;

		const fd = new FormData(form);

		


		const rq2 = new Request(`/api/session/${tokenID}/run`, {
			method: "POST",
			body: fd
		});



		
		const req = new PDBRedoApiRequest(`/api/session/${tokenID}/run`, {
			method: "POST",
			token: {
				id: tokenID,
				secret: form["token-secret"].value
			},
			body: fd
		});

		let statusOK;
		fetch(req).then(response => {
			statusOK = response.ok;
			return response.json();
		}).then(token => {
			if (statusOK)
			{
				const tokenForm = document.forms["fetch-token-result"];
				tokenForm["token-name"].value = token.name;
				tokenForm["token-expires"].value = token.expires;
			}
			else
				throw data.error;
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

		const req = new PDBRedoApiRequest(`/api/session/${this.token.id}/run`, {
			token: {
				id: this.token.id,
				secret: this.token.token
			}
		});

		let statusOK;
		fetch(req)
			.then(response => {
				statusOK = response.ok;
				return response.json()
			})
			.then(data => {
				if (! statusOK)
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

window.addEventListener('load', () => {
	new ApiTester();

	$(function () {
		$('[data-toggle="tooltip"]').tooltip();
	});
});
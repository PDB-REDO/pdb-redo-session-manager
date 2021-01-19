/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * 
 * Copyright (c) 2020 NKI/AVL, Netherlands Cancer Institute
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

import { PDBRedoApiRequest } from './request' ;
import 'bootstrap';
import bsCustomFileInput from 'bs-custom-file-input';

class ApiTester {
	constructor() {
		document.forms["token-form"].addEventListener("submit", (evt) => this.createToken(evt));
		document.forms["delete-token-form"].addEventListener("submit", (evt) => this.deleteToken(evt));
		document.forms["fetch-token-form"].addEventListener('submit', (evt) => this.fetchToken(evt));
		document.forms["submit-job-form"].addEventListener('submit', (evt) => this.submitJob(evt));
		document.forms["fetch-runs-form"].addEventListener('submit', (evt) => this.fetchRuns(evt));

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

		let statusOK;

		PDBRedoApiRequest.create(`/api/session/${tokenID}`, {
			method: "GET",
			token: {
				id: tokenID,
				secret: form["token-secret"].value
			}
		}).then(req => fetch(req))
		  .then(response => {
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

		let statusOK;

		PDBRedoApiRequest.create(`/api/session/${tokenID}/run`, {
			method: "POST",
			token: {
				id: tokenID,
				secret: form["token-secret"].value
			},
			body: fd
		}).then(req => fetch(req))
		  .then(response => {
			statusOK = response.ok;
			return response.json();
		}).then(job => {
			if (statusOK)
			{
				const submitResultForm = document.forms["submit-job-result"];
				submitResultForm["job-id"].value = job.id;
				submitResultForm["job-status"].value = job.status;
			}
			else if (job.error !== undefined)
				throw job.error;
			else
				throw 'unknown error';
		}).catch(err => {
			console.log(err);
			alert(`Failed to get token: ${err}`);
		});		
	}

	fetchRuns(e) {
		e.preventDefault();

		const form = document.forms["fetch-runs-form"];
		const tokenID = form["token-id"].value;

		const req = new PDBRedoApiRequest(`/api/session/${tokenID}/run`, {
			token: {
				id: tokenID,
				secret: form["token-secret"].value
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

				for (let run of data) {
					const row = document.createElement("tr");
					const td1 = document.createElement("td");
					td1.textContent = run.id;
					row.appendChild(td1);
					const td2 = document.createElement("td");
					td2.textContent = run.status;
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
	bsCustomFileInput.init();

	new ApiTester();

	$(function () {
		$('[data-toggle="tooltip"]').tooltip();
	});
});
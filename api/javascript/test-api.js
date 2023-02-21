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

import { PDBRedoApiRequest } from './PDBRedoRequest' ;
import 'bootstrap';
import bsCustomFileInput from 'bs-custom-file-input';

class ApiTester {
	constructor() {
		document.forms["submit-job-form"].addEventListener('submit', (evt) => this.submitJob(evt));
		document.forms["fetch-runs-form"].addEventListener('submit', (evt) => this.fetchRuns(evt));
		this.token = null;
	}

	submitJob(e) {
		e.preventDefault();

		const form = document.forms["submit-job-form"];
		const tokenID = form["token-id"].value;

		const fd = new FormData(form);

		let statusOK;

		PDBRedoApiRequest.create(`api/run`, {
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
		if (e) e.preventDefault();

		const form = document.forms["fetch-runs-form"];
		const tokenID = form["token-id"].value;

		const req = new PDBRedoApiRequest(`api/run`, {
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

					// a delete button
					const btn = document.createElement("button");
					btn.classList.add("btn");
					btn.classList.add("btn-sm");
					btn.classList.add("btn-secondary");
					btn.textContent = 'delete';
					btn.addEventListener('click', (e) => {
						this.deleteRun(tokenID, run.id);
					});

					const td3 = document.createElement("td");
					td3.appendChild(btn);
					row.appendChild(td3);
					row.setAttribute('data-run-id', run.id);

					tbody.appendChild(row);
				}

			}).catch(err => {
				console.log(err);
				alert("Failed to list runs: " + err);
			});
	}

	deleteRun(tokenID, runID) {
		const form = document.forms["fetch-runs-form"];

		PDBRedoApiRequest.create(`/api/run/${runID}`, {
			method: "DELETE",
			token: {
				id: tokenID,
				secret: form["token-secret"].value
			}
		}).then(req => fetch(req))
		  .then(response => {
			if (response.ok) {
				this.fetchRuns();
			}
			else {
				throw(`failed to delete run ${runID}`);
			}
		}).catch(err => {
			console.log(err);
			alert("Failed to get token " + err);
		});
	}
}

window.addEventListener('load', () => {
	bsCustomFileInput.init();

	new ApiTester();
});
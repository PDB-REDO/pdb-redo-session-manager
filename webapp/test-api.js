import { PDBRedoApiRequest } from './request' ;


class ApiTester {
	constructor() {
		document.forms["token-form"].addEventListener("submit", (evt) => this.createToken(evt));
		document.forms["delete-token-form"].addEventListener("submit", (evt) => this.deleteToken(evt));

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

		const payload = "This is an non-empty body";

		const req = new PDBRedoApiRequest(`/ajax/session/${this.token.id}?name=maarten&toestand=gek%20als%20een%20deur`, {
			method: "DELETE",
			body: payload,
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

}

window.addEventListener('load', () => new ApiTester());


class ApiTester {
	constructor() {
		console.log("hello, world!");

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
		}).then(reponse => {
			if (reponse.ok)
				return reponse.text();
			throw reponse;
		}).then(token => {
			this.token = token;
			const tokenForm = document.forms["delete-token-form"];
			tokenForm.token.value = token;
		}).catch(err => {
			console.log(err);
			alert("Failed to get token " + err);
		})
	}

	deleteToken(evt) {
		evt.preventDefault();

		const form = document.forms["delete-token-form"];
		const data = new FormData(form);

		fetch(form.action, {
			method: "DELETE",
			body: data
		}).then(reponse => {
			if (reponse.ok)
				return reponse.text();
			throw reponse.statusText;
		}).then(token => {
			this.token = token;
			const tokenForm = document.forms["delete-token-form"];
			tokenForm.token.value = token;
		}).catch(err => {
			console.log(err);
			alert("Failed to get token " + err);
		})
	}
}

window.addEventListener('load', () => new ApiTester());
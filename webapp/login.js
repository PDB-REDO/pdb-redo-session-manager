import '@fortawesome/fontawesome-free/css/all.min.css';
import 'bootstrap/js/dist/modal'

class LoginDialog {
	constructor() {
		const dlg = document.getElementById("login-dialog");

		if (dlg != null)
			this.setDialog(dlg);
		else
		{
			fetch("login-dialog")
				.then(dlg => dlg.text())
				.then(dlg => {
					const div = document.createElement("div");
					div.innerHTML = dlg;
					document.body.appendChild(div);

					// select the first div, which is the dialog just added
					this.setDialog(div.querySelector("div"));
				})
				.catch(e => {
					console.log(e);
					alert('Error displaying login dialog');
				});
		}
	}

	setDialog(dlg) {
		this.dialog = dlg;

		this.form = document.forms["local-login-form"];
		this.form.addEventListener('submit', (e) => this.submit(e));
		this.error = document.getElementById("failedToLoginMsg");

		this.show();
	}

	show() {
		$(this.error).hide();
		$(this.dialog).modal();
	}

	submit(evt) {
		if (evt != null) {
			evt.preventDefault();
		}

		this.form.password.value = btoa(this.form.password.value);
		this.form.submit();


		// const req = new Request(this.form.action);
		// const data = new FormData(this.form);
		// data.set("password", btoa(data.get("password")));

		// const today = new Date();
		// let dd = today.getDate();
		// if (dd < 10)
		// 	dd = `0${dd}`;
		// let mm = today.getMonth();
		// if (mm < 10)
		// 	mm = `0${mm}`;
		// const date = `${today.getFullYear()}${mm}${dd}`;

		// const username = document.getElementById("username").value;
		// const password = btoa(document.getElementById("password").value);

		// req.headers.append("Authorization", `PDB-REDO-login Credential=${username}/${nonce}/${date},Password=${password}`);

		// fetch(req).then(value => {
		// 	if (value.ok)
		// 		return value.json();
		// 	throw "failed";
		// }).then(value => {
		// 	if (value.ok) {
		// 		$(this.dialog).modal('hide');
		// 		if (this.callback != null)
		// 			this.callback();
		// 	}
		// 	else
		// 		throw "failed";
		// }).catch(err => {
		// 	$(this.error).show();
		// });
	}

	cancel() {
		window.location = "/";
	}
}

let loginDialog = null;

export function showLoginDialog() {
	if (loginDialog == null)
		loginDialog = new LoginDialog();
	loginDialog.show();
}

window.addEventListener('load', () => {
	showLoginDialog();
});
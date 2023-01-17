import { Modal } from 'bootstrap';

class LoginDialog {
	constructor() {
		const dlg = document.getElementById("login-dialog");

		if (dlg != null)
			this.setDialog(dlg);
		else {
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
		this.dialog = new Modal(dlg);

		this.form = document.forms["local-login-form"];
		this.error = document.getElementById("failedToLoginMsg");

		this.show();
	}

	show() {
		// $(this.error).hide();
		// $(this.dialog).modal();
		this.dialog.show();
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
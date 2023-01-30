import { Modal } from 'bootstrap';

class RegisterDialog {
	constructor() {
		const dlg = document.getElementById("register-dialog");

		this.dialog = new Modal(dlg);
		this.form = document.forms["local-reset-form"];
	
		this.show();
	}

	show() {
		this.dialog.show();
	}

	cancel() {
		window.location = "/";
	}
}

let registerDialog = null;

export function showRegisterDialog() {
	if (registerDialog == null)
		registerDialog = new RegisterDialog();
	registerDialog.show();
}

window.addEventListener('load', () => {
	showRegisterDialog();
});

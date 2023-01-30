import { Modal } from 'bootstrap';

class ResetPasswordDialog {
	constructor() {
		const dlg = document.getElementById("reset-dialog");

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

let resetpasswordDialog = null;

export function showResetPasswordDialog() {
	if (resetpasswordDialog == null)
		resetpasswordDialog = new ResetPasswordDialog();
	resetpasswordDialog.show();
}

window.addEventListener('load', () => {
	showResetPasswordDialog();
});

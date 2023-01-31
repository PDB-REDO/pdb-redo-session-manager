import { Modal } from 'bootstrap';

class Dialog {
	constructor(name) {
		const dlg = document.querySelector(`div[data-name='${name}']`);

		this.dialog = new Modal(dlg);
	
		this.form = dlg.querySelector('form');
		this.error = document.getElementById("failed-msg");

		this.submitted = false;
		this.form.addEventListener('submit', () => this.submitted = true )

		dlg.addEventListener('hidden.bs.modal', () => {
			if (this.submitted == false)
				window.location = "./"
		})
	}

	show() {
		this.dialog.show();
	}
}

let dialog = null;

export function showDialog() {
	if (dialog == null)
		dialog = new Dialog(dialog_name);
	dialog.show();
}

window.addEventListener('load', () => {
	if (dialog_name !== null)
		showDialog();
});
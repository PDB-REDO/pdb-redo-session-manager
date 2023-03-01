import { Modal } from 'bootstrap';

class Dialog {
	constructor(name) {
		const dlg = document.querySelector(`div[data-name='${name}']`);

		this.dialog = new Modal(dlg);

		this.form = dlg.querySelector('form');
		this.error = document.getElementById("failed-msg");

		this.submitted = false;
		this.form.addEventListener('submit', (e) => this.submit(e))

		dlg.addEventListener('hidden.bs.modal', () => {
			if (this.submitted == false)
				window.location = "./"
		});
	}

	submit() {
		this.submitted = true;
	}

	show() {
		this.dialog.show();
	}
}

class CCP4TokenDialog extends Dialog {
	constructor(name) {
		super(name);
	}

	submit(e) {
		e.preventDefault();

		const uri = this.form.elements['cburl'].value;
		const reqid = this.form.elements['reqid'].value;

		fetch(`token-request?name=${reqid}`, {
			method: 'post',
			credentials: 'include'
		}).then(r => {
			if (!r.ok)
				throw "Requesting a token failed";
			return r.json();
		}).then(r => {
			const form = document.createElement('form');
			form.method = 'POST';
			form.action = uri;
			
			const f1 = document.createElement('input');
			f1.type = 'hidden';
			f1.name = 'token-id';
			f1.value = r.id;
			form.appendChild(f1);
		
			const f2 = document.createElement('input');
			f2.type = 'hidden';
			f2.name = 'token-secret';
			f2.value = r.secret;
			form.appendChild(f2);

			const f3 = document.createElement('input');
			f3.type = 'hidden';
			f3.name = 'reqid';
			f3.value = reqid;
			form.appendChild(f3);

			document.body.appendChild(form);
			form.submit();
		}).catch(err => {
			alert(err);
			this.submitted = true;
		});
	}
}

window.addEventListener('load', () => {
	if (dialog_name !== null) {
		let dialog;
		if (dialog_name == "ccp4-token-request")
			dialog = new CCP4TokenDialog(dialog_name);
		else
			dialog = new Dialog(dialog_name);
		dialog.show();
	}
});
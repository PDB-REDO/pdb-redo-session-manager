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
};

class PasswordDialog extends Dialog {
	constructor(name, pw1, pw2) {
		super(name);

		const pwctl = this.form[pw1];
		pwctl.addEventListener('input', e => {
			const valid = this.isValidPassword(pwctl.value);
			pwctl.classList.toggle('is-invalid', !valid);
		});

		const pwctl2 = this.form[pw2];
		pwctl2.addEventListener('input', e => {
			const valid = pwctl.value === pwctl2.value;
			pwctl2.classList.toggle('is-invalid', !valid);
		});
	}

	isValidPassword(pw) {
		// calculate the password entropy, should be 50 or more
		const kMinimalEntropy = 50.0;
		const kSymbols = "!\"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~";
		let lowerSeen = false, upperSeen = false, digitSeen = false, symbolSeen = false;

		Array.from(pw).forEach(ch => {
			if (ch >= 'a' && ch <= 'z')
				lowerSeen = true;
			else if (ch >= 'A' && ch <= 'Z')
				upperSeen = true;
			else if (ch >= '0' && ch <= '9')
				digitSeen = true;
			else if (kSymbols.indexOf(ch) >= 0)
				symbolSeen = true;
			else if (" \t\n\r".indexOf(ch) >= 0)
				return false;
		});

		let poolSize = 0;
		if (lowerSeen)
			poolSize = 26;
		if (upperSeen)
			poolSize += 26;
		if (digitSeen)
			poolSize += 10;
		if (symbolSeen)
			poolSize += kSymbols.length;

		const entropy = Math.log2(poolSize) * pw.length;
		return entropy > kMinimalEntropy;
	}
};

window.addEventListener('load', () => {
	if (dialog_name !== null) {
		let dialog;
		if (dialog_name == "ccp4-token-request")
			dialog = new CCP4TokenDialog(dialog_name);
		else if (dialog_name == "register")
			dialog = new PasswordDialog(dialog_name, "password", "password-2");
		else if (dialog_name == "change")
			dialog = new PasswordDialog(dialog_name, "new-password", "new-password-2");
		else
			dialog = new Dialog(dialog_name);
		dialog.show();
	}
});
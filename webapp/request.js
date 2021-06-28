import CryptoES from "crypto-es";

export class PDBRedoApiRequest extends Request {
	constructor(input, init, bodyhash) {

		// decode the input url, parse out the pathname, params, etc
		var url = document.createElement('a');
		url.href = input;

		const headers = new Headers(init.headers);

		const query = url.search;

		// sort the params
		const params = query
			? (/^[?#]/.test(query) ? query.slice(1) : query)
				.split('&')
				.map(param => {
					let [key, value] = param.split('=');
					value = value ? decodeURIComponent(value.replace(/\+/g, ' ')) : '';
					return { key: key, value: value };
				})
				.sort((a, b) => a.key < b.key ? -1 : 1)
				.reduce((params, param) => {
					params.push(`${encodeURIComponent(param.key)}=${encodeURIComponent(param.value)}`);
					return params;
				}, [])
				.join('&')
			: "";

		if (typeof(init.method) != 'string')
			init.method = 'GET';

		let contentHash = bodyhash;

		if (contentHash === undefined) {
			switch (typeof(init.body))
			{
				case 'undefined':
					contentHash = CryptoES.SHA256('');
					break;
	
				case 'string':
						contentHash = CryptoES.SHA256(init.body);
					break;
				
				// FormData?
				case 'object':
					if (init.body instanceof FormData)
						throw "Use PDBRedoApiRequest.create() to create a request with a FormData body";
	
				default:
					throw "PDBRedoApiRequest only accepts a string body";
			}
		}

		const canonicalRequest = [init.method, url.pathname, params, url.host, contentHash.toString(CryptoES.enc.Base64)].join("\n");
		const canonicalRequestHash = CryptoES.SHA256(canonicalRequest);

		// create the scope
		const token = init.token;
		delete init.token;

		const now = new Date();
		const timestamp = now.toISOString();
		const year = `${now.getFullYear()}`;
		const month = now.getMonth() < 9 ? `0${now.getMonth() + 1}` : `${now.getMonth() + 1}`;
		const day = now.getDate() < 10 ? `0${now.getDate()}` : `${now.getDate()}`;
		const date = year + month + day;
		
		const credential = `${token.id}/${date}/pdb-redo-api`;

		const stringToSign = ["PDB-REDO-api", timestamp, credential, canonicalRequestHash.toString(CryptoES.enc.Base64) ].join("\n");
		const key = CryptoES.HmacSHA256(date, `PDB-REDO${token.secret}`);
		const signature = CryptoES.HmacSHA256(stringToSign, key);

		headers.append('Authorization', 
			`PDB-REDO-api Credential=${credential},SignedHeaders=host;x-pdb-redo-content-sha256,Signature=${signature.toString(CryptoES.enc.Base64)}`);
		headers.append('X-PDB-REDO-Date', timestamp);
		init.headers = headers;

		super(input, init);
	}

	static create(input, init) {

		if (init.body instanceof FormData) {
			return Promise.all([...init.body]
					.map(e => e[1])
					.filter(v => v instanceof File)
					.map(f => f.arrayBuffer()))
				.then(files => {
					const sep = `${sha256(Math.random() + '-' + Math.random()).toString(CryptoES.enc.Base64)}`;

					const h = CryptoES.algo.SHA256.create();
					const fields = [];
				
					let fi = 0;
					for (let p of init.body) {
						let [name, value] = p;
				
						if (value instanceof File) {
							const buffer = files[fi++];

							const f1 = `--${sep}\r\nContent-Disposition: form-data; name="${name}"; filename="${value.name}"\r\nContent-Type: application/octet-stream\r\n\r\n`;
							const f2 = "\r\n";

							fields.push(f1);
							fields.push(buffer);
							fields.push(f2);

							const wa = CryptoES.lib.WordArray.create(buffer);

							h.update(f1);
							h.update(wa);
							h.update(f2);
						}
						else {
							const f = `--${sep}\r\nContent-Disposition: form-data; name="${name}"\r\n\r\n${value}\r\n`;
							fields.push(f);
							h.update(f);
						}
					}
				
					const tail = `--${sep}--\r\n`; 
					fields.push(tail);
					h.update(tail);
				
					const hs = h.finalize().toString(CryptoES.enc.Base64);
					const blob = new Blob(fields);
				
					init.body = blob;

					if (init.headers === undefined)
						init.headers = new Headers();

					init.headers.set('Content-Type', `multipart/form-data; boundary=${sep}`);

					return new PDBRedoApiRequest(input, init, hs);
				});
		}
		else {
			return new Promise((resolve) => {
				resolve(new PDBRedoApiRequest(input, init));
			});
		}
	}
}
import sha256 from "crypto-js/sha256";
import hmacSHA256 from "crypto-js/hmac-sha256";
import Base64 from "crypto-js/enc-base64";

export class PDBRedoApiRequest extends Request {
	constructor(input, init) {

		// decode the input url, parse out the pathname, params, etc
		var url = document.createElement('a');
		url.href = input;
		
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

		let contentHash;

		switch (typeof(init.body))
		{
			case 'undefined':
				contentHash = sha256('');
				break;

			case 'string':
				contentHash = sha256(init.body);
				break;
			
			default:
				throw "PDBRedoApiRequest only accepts a string body";
		}

		const canonicalRequest = [init.method, url.pathname, params, url.host, contentHash.toString(Base64)].join("\n");
		const canonicalRequestHash = sha256(canonicalRequest);

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

		const stringToSign = ["PDB-REDO-api", timestamp, credential, canonicalRequestHash.toString(Base64) ].join("\n");
		const key = hmacSHA256(date, `PDB-REDO${token.secret}`);
		const signature = hmacSHA256(stringToSign, key);

		const headers = new Headers(init.headers);
		headers.append('Authorization', 
			`PDB-REDO-api Credential=${credential},SignedHeaders=host;x-pdb-redo-content-sha256,Signature=${signature.toString(Base64)}`);
		headers.append('X-PDB-REDO-Date', timestamp);
		init.headers = headers;

		super(input, init);
	}
}
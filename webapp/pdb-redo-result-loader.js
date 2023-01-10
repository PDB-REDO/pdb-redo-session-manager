import { WebComponents } from '@webcomponents/webcomponentsjs'

WebComponents = WebComponents || {
	waitFor(cb) { addEventListener('WebComponentsReady', cb) }
}

WebComponents.waitFor(async () => {
	let loaderURL = document.querySelector('script[src$="pdb-redo-result-loader.js"]').src;
	loaderURL = loaderURL.replace(/pdb-redo-result-loader\.js$/, '');
	__webpack_public_path__ = loaderURL;

	import('./pdb-redo-result');
});


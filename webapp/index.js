import '@babel/polyfill'
import '@fortawesome/fontawesome-free/css/all.min.css';
import 'bootstrap';
// import 'bootstrap/js/dist/modal'
// import * as d3 from 'd3';
// import $ from 'jquery';
import { showLoginDialog } from './login';
import './pdb-redo-bootstrap.scss';

window.addEventListener("load", () => {
	if (typeof username === 'undefined' || username == null)
		showLoginDialog();
});



window.addEventListener('load', () => {
	const entry = document.querySelector('#entry-content');
	const component = document.querySelector('pdb-redo-result');

	if (entry != null) {
		component.putEntry(entry.innerHTML);
	}

});
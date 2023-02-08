window.addEventListener('load', () => {
	const table = document.querySelector('#jobs-table');
	const rows = table.querySelectorAll("tr");
	Array.from(rows).forEach(tr => {
		tr.addEventListener('click', (e) => {
			const jobID = tr.dataset.job;
			window.location = `job/result/${jobID}`;
		})
	})
});
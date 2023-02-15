window.addEventListener('load', () => {

	const table = document.querySelector('#jobs-table');
	const rows = table.querySelectorAll("tr.done");
	Array.from(rows).forEach(tr => {
		tr.addEventListener('click', (e) => {
			const jobID = tr.dataset.job;
			window.location = `job/result/${jobID}`;
		})
	});

	[...document.querySelectorAll('a.delete-a')]
		.forEach(btn => {
			btn.addEventListener('click', (e) => {
				e.preventDefault();
				e.stopPropagation();

				let failed = false;

				if (confirm(`Are you sure you want to delete job ${btn.dataset.id}?`))
				{
					fetch(`job/${btn.dataset.id}`, {
						method: "DELETE",
						credentials: 'include'
					}).then(r => {
						if (r.ok)
							window.location.replace("job");
						else
							failed = true;
						return r.text();
					}).then(r => {
						if (failed)
							throw r;
					}).catch(err => {
						console.log(err);
					});
				}
			});
		})
});

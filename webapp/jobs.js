window.addEventListener('load', () => {
	const table = document.querySelector('#jobs-table');
	if (table) {

		const jobIDs = [];

		const rows = table.querySelectorAll("tr.job-row");
		Array.from(rows).forEach(tr => {
			const jobID = tr.dataset.job;
			const status = tr.dataset.status;

			if (status == 'started' || status == 'registered' || status == 'starting' || status == 'queued' || status == 'running')
				jobIDs.push(jobID);

			tr.addEventListener('click', (e) => {
				window.location = `job/result/${jobID}`;
			})
		});

		[...document.querySelectorAll('a.delete-a')]
			.forEach(btn => {
				btn.addEventListener('click', (e) => {
					e.preventDefault();
					e.stopPropagation();

					let failed = false;

					if (confirm(`Are you sure you want to delete job ${btn.dataset.id}?`)) {
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
			});

		if (jobIDs.length > 0) {
			const url = encodeURI(`job/status?ids=[${jobIDs.join(",")}]`);

			setInterval(() => {
				fetch(url, { credentials: 'include' })
					.then(r => {
						if (r.ok)
							return r.json();
						throw 'no data';
					}).then(r => {
						
						if (r.findIndex((s) => s == 'stopped' || s == 'ended') != -1)
							window.location.reload();

					}).catch(err => console.log(err));
			}, 15000);

		}
	}
});

window.addEventListener('load', () => {

	const form = document.querySelector("#submit-job-form");
	form.addEventListener('submit', (e) => {
		e.preventDefault();

		const data = new FormData(form);

		fetch(form.action, {
			method: "POST",
			body: data
		}).then(response => {
			if (response.ok)
				window.location = "job";
			else
				throw "Server replied with an error";
		}).catch(err => {
			console.log(err);
			alert("Failed to submit job: " + err);
		});
	});

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

window.addEventListener('load', () => {

	[...document.querySelectorAll('a.delete-a')]
		.forEach(btn => {
			btn.addEventListener('click', async (e) => {
				e.preventDefault();
				e.stopPropagation();

				if (confirm(`Are you sure you want to delete token ${btn.dataset.id}?`)) {
					const r = await fetch(`token?id=${btn.dataset.id}`, {
						method: "DELETE",
						credentials: 'include',
						headers: {
							'X-CSRF-Token': _csrf
						}
					})
					
					if (r.ok)
						window.location.reload();
					else
					{
						const txt = await r.text();
						console.log(`Deleting token failed with: ${txt}`);

						alert("Deleting the token failed");
					}
				}
			});
		})
});


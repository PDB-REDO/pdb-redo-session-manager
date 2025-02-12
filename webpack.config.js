const { CleanWebpackPlugin } = require('clean-webpack-plugin');
const path = require('path');
const MiniCssExtractPlugin = require('mini-css-extract-plugin');

const SCRIPTS = path.resolve(__dirname, "webapp");
const DEST = path.resolve(__dirname, "docroot");

module.exports = (env) => {

	const PRODUCTION = env != null && env.PRODUCTION;

	const webpackConf = {
		entry: {
			admin: path.resolve(SCRIPTS, "admin.js"),
			dialog: path.resolve(SCRIPTS, "dialog.js"),
			index: path.resolve(SCRIPTS, "index.js"),
			'inline-entry': path.resolve(SCRIPTS, 'inline-entry.js'),
			jobs: path.resolve(SCRIPTS, 'jobs.js'),
			'pdb-redo-result': path.resolve(SCRIPTS, 'pdb-redo-result.js'),
			'pdb-redo-result-loader': path.resolve(SCRIPTS, 'pdb-redo-result-loader.js'),
			tokens: path.resolve(SCRIPTS, "tokens.js"),

			w3: path.resolve(SCRIPTS, "w3.css"),
			'web-component-style': { import: path.resolve(SCRIPTS, 'web-component-style.scss') }
		},

		output: {
			path: DEST,
			crossOriginLoading: 'anonymous',
			filename: "scripts/[name].js"
		},

		module: {
			rules: [
				{
					test: /\.js/,
					exclude: /node_modules/,
					use: {
						loader: "babel-loader",
						options: {
							presets: ['@babel/preset-env']
						}
					}
				},

				{
					test: /\.(sa|sc|c)ss$/i,
					use: [
						MiniCssExtractPlugin.loader,
						"css-loader",
						"postcss-loader",
						"sass-loader"
					]
				},

				{
					test: /\.woff(2)?(\?v=[0-9]\.[0-9]\.[0-9])?$/,
					include: path.resolve(__dirname, './node_modules/bootstrap-icons/font/fonts'),
					type: 'asset/resource',
					generator: {
						filename: 'fonts/[name][ext]'
					}
				}
			]
		},

		resolve: {
			extensions: ['.js', '.css', '.scss'],
		},

		optimization: { minimizer: [] },

		target: 'web',

		plugins: [
			new MiniCssExtractPlugin({
				filename: "css/[name].css"
			})
		]
	};

	if (PRODUCTION) {
		webpackConf.mode = "production";

		webpackConf.plugins.push(
			new CleanWebpackPlugin({
				verbose: true,
				cleanOnceBeforeBuildPatterns: [
					'css/*',
					'fonts/*',
					'scripts/*',
				]

			})
		);
	} else {
		webpackConf.mode = "development";
		webpackConf.devtool = 'source-map';
	}

	return webpackConf;
};

const MiniCssExtractPlugin = require('mini-css-extract-plugin');
const { CleanWebpackPlugin } = require('clean-webpack-plugin');
const webpack = require('webpack');
const TerserPlugin = require('terser-webpack-plugin');
const UglifyJsPlugin = require('uglifyjs-webpack-plugin');
const path = require('path');

const SCRIPTS = __dirname + "/webapp/";
const DEST = __dirname + "/docroot/scripts";

module.exports = (env) => {

	const PRODUCTION = env != null && env.PRODUCTION;

	const webpackConf = {
		entry: {
			admin: SCRIPTS + "admin.js",
			dialog: SCRIPTS + "dialog.js",
			index: SCRIPTS + "index.js",
			'inline-entry': SCRIPTS + 'inline-entry.js',
			jobs: SCRIPTS + 'jobs.js',
			'pdb-redo-result': SCRIPTS + 'pdb-redo-result.js',
			'pdb-redo-result-loader': SCRIPTS + 'pdb-redo-result-loader.js',
			tokens: SCRIPTS + "tokens.js",

			'web-component-style': { import: SCRIPTS + 'web-component-style.scss' }
		},

		output: {
			path: DEST
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
						filename: '../fonts/[name][ext]'
					}
				}
			]
		},

		resolve: {
			extensions: ['.js', '.scss'],
		},

		plugins: [
			new MiniCssExtractPlugin({
				filename: "../css/[name].css"
			})
		],

		optimization: { minimizer: [] }
	};

	if (PRODUCTION) {
		webpackConf.mode = "production";

		webpackConf.plugins.push(
			new CleanWebpackPlugin({
				cleanOnceBeforeBuildPatterns: [
					'scripts',
					'fonts'
				]
			}));

		webpackConf.optimization.minimizer.push(
			new TerserPlugin({ /* additional options here */ }),
			new UglifyJsPlugin({ parallel: 4 })
		);
	} else {
		webpackConf.mode = "development";
		webpackConf.devtool = 'source-map';
		webpackConf.plugins.push(new webpack.optimize.AggressiveMergingPlugin())
	}

	return webpackConf;
};

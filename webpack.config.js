const MiniCssExtractPlugin = require('mini-css-extract-plugin');
const { CleanWebpackPlugin } = require('clean-webpack-plugin');
const webpack = require('webpack');
const TerserPlugin = require('terser-webpack-plugin');
const UglifyJsPlugin = require('uglifyjs-webpack-plugin');
const path = require('path');

const SCRIPTS = __dirname + "/webapp/";
const SCSS = __dirname + "/scss/";
const DEST = __dirname + "/docroot/";

module.exports = (env) => {

	const PRODUCTION = env != null && env.PRODUCTION;

	const webpackConf = {
		entry: {
			// 'pdb-redo-bootstrap': SCSS + "pdb-redo-bootstrap.scss",
			// 'pdb-redo-result': SCSS + "pdb-redo-result.scss",

			admin: SCRIPTS + "admin.js",
			index: SCRIPTS + "index.js",
			'job-overview': SCRIPTS + 'job-overview.js',
			dialog: SCRIPTS + "dialog.js",
			login: SCRIPTS + "login.js",
			'reset-pw': SCRIPTS + "reset-pw.js",
			register: SCRIPTS + "register.js",
			request: SCRIPTS + "request.js",
			"test-api": SCRIPTS + "test-api.js",

			'pdb-redo-result': SCRIPTS + 'pdb-redo-result.js',
			'pdb-redo-result-loader': SCRIPTS + 'pdb-redo-result-loader.js'
		},

		output: {
			path: DEST + "/scripts/",
			filename: "./[name].js"
		},

		devtool: "source-map",

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
					test: /pdb-redo-result\.scss$/,
					use: [
						'css-loader',
						'sass-loader'
					]
				},
				{
					test: /pdb-redo-bootstrap\.scss$/,
					use: [
						// PRODUCTION ? MiniCssExtractPlugin.loader : "style-loader",
						"style-loader",
						'css-loader',
						'sass-loader'
					]
				},
				// {
				// 	test: /\.(sa|sc|c)ss$/,
				// 	use: [
				// 		PRODUCTION ? MiniCssExtractPlugin.loader : "style-loader",
				// 		'css-loader',
				// 		'sass-loader'
				// 	]
				// },

				{
					test: /\.woff(2)?(\?v=[0-9]\.[0-9]\.[0-9])?$/,
					include: path.resolve(__dirname, './node_modules/bootstrap-icons/font/fonts'),
					type: 'asset/resource',
					generator: {
						filename: 'fonts/[name][ext][query]'
					}
				},

				{
					test: /\.(png|jpg|gif)$/,
					use: [
						{
							loader: 'file-loader',
							options: {
								outputPath: "css/images",
								publicPath: "images/"
							},
						},
					]
				}
			]
		},

		resolve: {
			extensions: ['.tsx', '.ts', '.js'],
		},

		plugins: [
			// new CleanWebpackPlugin({
			// 	cleanOnceBeforeBuildPatterns: [
			// 		'scripts/**/*',
			// 		'fonts/**/*'
			// 	]
			// }),
			new MiniCssExtractPlugin({
				filename: './css/[name].css',
				chunkFilename: './css/[id].css'
			})
		],

		optimization: {
			minimizer: [
				new TerserPlugin({ /* additional options here */ }),
				new UglifyJsPlugin({ parallel: 4 })
			]
		}
	};

	if (PRODUCTION) {
		webpackConf.mode = "production";

		webpackConf.plugins.push(
			new CleanWebpackPlugin({
				cleanOnceBeforeBuildPatterns: [
					'scripts/**/*',
					'fonts/**/*'
				]
			}));
	} else {
		webpackConf.mode = "development";
		webpackConf.devtool = 'source-map';
		webpackConf.plugins.push(new webpack.optimize.AggressiveMergingPlugin())
	}

	return webpackConf;
};

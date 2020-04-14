const { CleanWebpackPlugin } = require('clean-webpack-plugin');
const UglifyJsPlugin = require('uglify-js-plugin');
const webpack = require("webpack");

const SCRIPTS = __dirname + "/webapp/";
const DEST = __dirname + "/docroot/scripts/";

module.exports = (env) => {

	const webpackConf = {
		entry: {
			polyfill: '@babel/polyfill',
			admin: SCRIPTS + "admin.js",
			index: SCRIPTS + "index.js",
			login: SCRIPTS + "login.js",
			request: SCRIPTS + "request.js",
			"test-api": SCRIPTS + "test-api.js"
		},

		module: {
			rules: [
				{
					test: /\.js$/,
					exclude: /node_modules/,
					use: {
						loader: "babel-loader",
						options: {
							presets: ['@babel/preset-env']
						}
					}
				},
				{
					test: /\.css$/,
					use: ['style-loader', 'css-loader']
				},
				{
					test: /\.(eot|svg|ttf|woff(2)?)(\?v=\d+\.\d+\.\d+)?/,
					loader: 'file-loader',
					options: {
						name: '[name].[ext]',
						outputPath: '../fonts/',
						publicPath: 'fonts/'
					}
				},

				{
					test: /\.s[ac]ss$/i,
					use: [
					  // Creates `style` nodes from JS strings
					  'style-loader',
					  // Translates CSS into CommonJS
					  'css-loader',
					  // Compiles Sass to CSS
					  'sass-loader'
					]
				  }
			]
		},

		output: {
			path: DEST,
			filename: "[name].js"
		},

		plugins: [
			new CleanWebpackPlugin(),
			new webpack.ProvidePlugin({
				$: 'jquery',
				jQuery: 'jquery'
			})
		]
	};

    const PRODUCTIE = env != null && env.PRODUCTIE;

	if (PRODUCTIE) {
		webpackConf.mode = "production";
		webpackConf.plugins.push(new UglifyJsPlugin({parallel: 4}))
	} else {
		webpackConf.mode = "development";
		webpackConf.devtool = 'source-map';
		webpackConf.plugins.push(new webpack.optimize.AggressiveMergingPlugin())
	}

	return webpackConf;
};

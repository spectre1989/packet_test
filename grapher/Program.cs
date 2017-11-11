using System.IO;
using Newtonsoft.Json.Linq;
using System;
using System.Collections.Generic;

namespace grapher
{
    class Program
    {
        static void Main(string[] args)
        {
            foreach(string arg in args)
            {
                if(File.Exists(arg))
                {
                    CreateReport(arg);
                }
                else if(Directory.Exists(arg))
                {
                    ProcessDir(arg);
                }
            }

            Console.Write("done");
        }

        static void ProcessDir(string dir)
        {
            foreach (string file in Directory.GetFiles(dir))
            {
                CreateReport(file);
            }
            foreach (string subdir in Directory.GetDirectories(dir))
            {
                ProcessDir(subdir);
            }
        }

        struct TestInfo
        {
            public float packet_loss_percentage;
            public UInt32 packets_per_second;
            public UInt32 bytes_per_second;
        }

        static void CreateReport(string path)
        {
            string extension = Path.GetExtension(path);
            if(extension != ".txt" && extension != ".json")
            {
                Console.WriteLine("ignoring " + path);
                return;
            }

            string out_file_path = Path.ChangeExtension(path, "html");
            if(File.Exists(out_file_path))
            {
                File.Delete(out_file_path);
            }
            FileStream out_file_stream = File.OpenWrite(out_file_path);
            StreamWriter writer = new StreamWriter(out_file_stream);
            writer.Write("<html><head><script type = \"text/javascript\" src = \"https://www.gstatic.com/charts/loader.js\"></script><script type = \"text/javascript\">google.charts.load('current', { 'packages':['corechart'] });google.charts.setOnLoadCallback(drawChart); function drawChart(){");

            string divs = "<h1>" + Path.GetFileNameWithoutExtension(path) + "</h1>";

            JObject root = JObject.Parse(new StreamReader(File.OpenRead(path)).ReadToEnd());
            JArray tests = root["tests"] as JArray;
            List<TestInfo> test_info = new List<TestInfo>();
            for (int test_i = 0; test_i < tests.Count; ++test_i)
            {
                JObject test = tests[test_i] as JObject;
                UInt32 duration_s = test["duration_s"].ToObject<UInt32>();
                UInt32 packets_per_s = test["packets_per_s"].ToObject<UInt32>();
                UInt32 packet_size = test["packet_size"].ToObject<UInt32>();
                UInt32 num_packets_dropped = test["num_packets_dropped"].ToObject<UInt32>();
                UInt32 num_packets_duplicated = test["num_packets_duplicated"].ToObject<UInt32>();
                UInt32 num_packets = duration_s * packets_per_s;
                float packet_loss_perc = (num_packets_dropped * 100) / (float)num_packets;
                float packet_dupe_perc = (num_packets_duplicated * 100) / (float)num_packets;

                TestInfo info;
                info.packet_loss_percentage = packet_loss_perc;
                info.packets_per_second = packets_per_s;
                info.bytes_per_second = packets_per_s * packet_size;
                test_info.Add(info);


                // Title
                divs += string.Format("<h2>{0} packets per second for {1} seconds, {2} bytes per packet, {3} packets lost({4}%)",
                    packets_per_s, duration_s, packet_size, num_packets_dropped, packet_loss_perc);
                if (num_packets_duplicated > 0)
                {
                    divs += ", " + num_packets_duplicated.ToString() + " packets duplicated(" + packet_dupe_perc.ToString() + "%)";
                }
                divs += "</h2>";


                // Begin chart
                Chart chart = new Chart();
                chart.title = "RTT and Packet Loss";
                chart.x_axis = "Time (s)";
                chart.name = "rttChart" + test_i.ToString();
                List<string> y_axes = new List<string>(new string[] { "RTT (ms)" });
                List<Chart.Series> series = new List<Chart.Series>(new Chart.Series[] { Chart.CreateSeries("Min", "area", 0), Chart.CreateSeries("Max", "area", 0), Chart.CreateSeries("Avg", "area", 0) });
                if (num_packets_dropped > 0)
                {
                    y_axes.Add("Num Packets");
                    series.Add(Chart.CreateSeries("Dropped Packets", "bars", 1));
                }
                chart.y_axes = y_axes.ToArray();
                chart.series = series.ToArray();
                BeginChart(ref chart, writer);


                const uint c_max_points_per_graph = 1000;
                uint num_packets_in_slice = 0;
                float min_rtt_ms = 0.0f;
                float max_rtt_ms = 0.0f;
                float total_rtt_ms = 0.0f;
                uint num_packets_dropped_in_slice = 0;
                uint stride = Math.Max(1, num_packets / c_max_points_per_graph);


                JArray packets = test["packets"] as JArray;
                for (int i = 0; i < packets.Count; ++i)
                {
                    float rtt_ms = packets[i].ToObject<float>() * 1000.0f;

                    if (rtt_ms >= 0.0f)
                    {
                        ++num_packets_in_slice;
                        total_rtt_ms += rtt_ms;
                        if (num_packets_in_slice == 1)
                        {
                            max_rtt_ms = rtt_ms;
                            min_rtt_ms = rtt_ms;
                        }
                        else
                        {
                            max_rtt_ms = Math.Max(max_rtt_ms, rtt_ms);
                            min_rtt_ms = Math.Min(min_rtt_ms, rtt_ms);
                        }
                    }
                    else
                    {
                        ++num_packets_dropped_in_slice;
                    }

                    if ((i + 1) % stride == 0 || i == (num_packets - 1))
                    {
                        float avg_rtt_ms = total_rtt_ms;
                        if (num_packets_in_slice > 0)
                        {
                            avg_rtt_ms /= num_packets_in_slice;
                        }

                        writer.Write(string.Format(",[{0}, {1}, {2}, {3}", (i - stride + 1) / (float)packets_per_s, min_rtt_ms, max_rtt_ms, avg_rtt_ms));
                        if (num_packets_dropped > 0)
                        {
                            writer.Write(", " + num_packets_dropped_in_slice.ToString());
                        }
                        writer.Write("]");

                        num_packets_in_slice = 0;
                        num_packets_dropped_in_slice = 0;
                        total_rtt_ms = 0.0f;
                    }
                }
                
                
                uint chart_width = c_max_points_per_graph * 4;
                EndChart(ref chart, writer, ref divs, chart_width);
            }

            // packet loss graph
            Chart overallChart = new Chart();
            overallChart.title = "Packet Loss";
            overallChart.x_axis = "Packet Loss";
            overallChart.name = "packetLoss";
            overallChart.title = "Packet Loss";
            overallChart.y_axes = new string[] { "Bytes Per Second", "Packets Per Second" };
            overallChart.series = new Chart.Series[] { Chart.CreateSeries("Bytes", "line", 0), Chart.CreateSeries("Packets", "line", 1) };
            BeginChart(ref overallChart, writer);
            test_info.Sort(delegate (TestInfo a, TestInfo b) { return a.packet_loss_percentage.CompareTo(b.packet_loss_percentage); });
            foreach (TestInfo info in test_info)
            {
                writer.Write(string.Format(",[{0}, {1}, {2}]", info.packet_loss_percentage, info.bytes_per_second, info.packets_per_second));
            }
            uint c_chart_width = 800;
            EndChart(ref overallChart, writer, ref divs, c_chart_width);


            writer.WriteLine("}</script></head><body>" + divs + "</body></html>");
            writer.Flush();
            out_file_stream.Close();
        }

        struct Chart
        {
            public string name;
            public string x_axis;
            public Series[] series;
            public string[] y_axes;
            public string title;

            public struct Series
            {
                public string name;
                public string type;
                public int y_axis;
            }

            public static Series CreateSeries(string name, string type, int y_axis)
            {
                Series s;
                s.name = name;
                s.type = type;
                s.y_axis = y_axis;
                return s;
            }
        }

        static void BeginChart(ref Chart chart, StreamWriter writer)
        {
            writer.Write("var " + chart.name + "_data = google.visualization.arrayToDataTable([['" + chart.x_axis + "'");
            foreach(Chart.Series series in chart.series)
            {
                writer.Write(", '" + series.name + "'");
            }
            writer.Write("]");
        }

        static void EndChart(ref Chart chart, StreamWriter writer, ref string divs, uint width)
        {
            writer.WriteLine("]);");

            writer.WriteLine("var " + chart.name + "_options = {title: '" + chart.title + "', width: '" + width.ToString() + "px', height: '800px', pointSize: 1,");

            string seriesString = "";
            for(int i = 0; i < chart.series.Length; ++i)
            {
                Chart.Series series = chart.series[i];

                if(i > 0)
                {
                    seriesString += ",";
                }

                seriesString += i.ToString() + ": {targetAxisIndex: '" + series.y_axis.ToString() + "', type: '" + series.type + "'}";
            }

            string axesString = "";
            for (int i = 0; i < chart.y_axes.Length; ++i)
            {
                string y_axis = chart.y_axes[i];

                if(i > 0)
                {
                    axesString += ",";
                }

                axesString += i.ToString() + ": {title: '" + y_axis + "'}";
            }

            writer.WriteLine("series: {" + seriesString + "}, vAxes: {" + axesString + "}");
            writer.WriteLine("};");

            writer.WriteLine(string.Format("var {0}_chart = new google.visualization.ComboChart(document.getElementById('{0}_div'));", chart.name));
            writer.WriteLine(string.Format("{0}_chart.draw({0}_data, {0}_options); ", chart.name));

            divs += string.Format("<div id = \"{0}_div\" style=\"width: {1}px; height: 800px; \"></div>", chart.name, width);
        }
    }
}
